/* =========================================================================
* This file is part of the CSM SICD Plugin
* =========================================================================
*
* (C) Copyright 2004 - 2011, General Dynamics - Advanced Information Systems
*
* The CSM SICD Plugin is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this program; if not,
* see <http://www.gnu.org/licenses/>.
*
*/

#include "Error.h"
#include <sys/OS.h>
#include <sys/Path.h>
#include <six/csm/SIDDSensorModel.h>
#include <io/StringStream.h>
#include <logging/NullLogger.h>
#include <six/XMLControlFactory.h>
#include <six/NITFReadControl.h>
#include <six/sidd/DerivedXMLControl.h>
#include <six/sidd/Utilities.h>

namespace six
{
namespace CSM
{
const csm::Version SIDDSensorModel::VERSION(1, 0, 3);
const char SIDDSensorModel::NAME[] = "SIDD_SENSOR_MODEL";

SIDDSensorModel::SIDDSensorModel(const csm::Isd& isd,
                                 const std::string& dataDir)
{
    setSchemaDir(dataDir);

    // Support multi-segment SIDDs
    // In this case, the ISD should tell us which image it wants to use if it's
    // not the first one
    const std::string imageIndexStr(isd.param(IMAGE_INDEX_PARAM));
    size_t imageIndex;
    if (imageIndexStr.empty())
    {
        imageIndex = 0;
    }
    else if (!str::isNumeric(imageIndexStr))
    {
        throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                "Unexpected " + std::string(IMAGE_INDEX_PARAM) +
                        " parameter: " + imageIndexStr,
                "SIDDSensorModel::SIDDSensorModel");
    }
    else
    {
        imageIndex = str::toType<size_t>(imageIndexStr);
    }

    // Based on the ISD format, initialize as appropriate
    const std::string& format(isd.format());
    if (format == "NITF2.1")
    {
        initializeFromISD(dynamic_cast<const csm::Nitf21Isd&>(isd),
                          imageIndex);
    }
    else if (format == "FILENAME")
    {
        // Note: this case has not been tested
        initializeFromFile(isd.filename(), imageIndex);
    }
    else
    {
        throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                           "Unsupported ISD format " + format,
                           "SIDDSensorModel::constructModelFromISD");
    }

}

SIDDSensorModel::SIDDSensorModel(const std::string& sensorModelState,
                                 const std::string& dataDir)
{
    setSchemaDir(dataDir);
    replaceModelStateImpl(sensorModelState);
}

void SIDDSensorModel::initializeFromFile(const std::string& pathname,
                                         size_t imageIndex)
{
    try
    {
        // create an XML registry
        // The reason to do this is to avoid adding XMLControlCreators to the
        // XMLControlFactory singleton - this way has more fine-grained control
        six::XMLControlRegistry xmlRegistry;
        xmlRegistry.addCreator(six::DataType::DERIVED,
                new six::XMLControlCreatorT<six::sidd::DerivedXMLControl>());

        // create a reader and load the file
        six::NITFReadControl reader;
        reader.setXMLControlRegistry(&xmlRegistry);
        reader.load(pathname, mSchemaDirs);

        // For multi-image SIDDs, all the SIDD DESs will appear first (in the
        // case where SICD DESs are also present), so we just have to grab out
        // the Nth Data object
        six::Container* const container = reader.getContainer();
        if (container->getDataType() != six::DataType::DERIVED ||
            container->getNumData() < imageIndex + 1)
        {
            throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                               "Not a SIDD",
                               "SIDDSensorModel::initializeFromFile");
        }

        six::Data* const data = container->getData(imageIndex);
        if (data->getDataType() != six::DataType::DERIVED)
        {
            throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                               "Not a SIDD",
                               "SIDDSensorModel::initializeFromFile");
        }

        // Cast it and take ownership
        mData.reset(reinterpret_cast<six::sidd::DerivedData*>(data));
        container->removeData(mData.get());

        // get xml as string for sensor model state
        std::string xmlStr = six::toXMLString(mData.get(), &xmlRegistry);
        mSensorModelState = NAME + std::string(" ") + xmlStr;
        reinitialize();
    }
    catch (const except::Exception& ex)
    {
        throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                           ex.getMessage(),
                           "SIDDSensorModel::initializeFromFile");
    }
}

void SIDDSensorModel::initializeFromISD(const csm::Nitf21Isd& isd,
                                        size_t imageIndex)
{
    try
    {
        // Check for the SIDD DES associated with imageIndex and parse it
        // DES's are always in the same order as the images, so we just have to
        // find the Nth DES
        xml::lite::Document* siddXML = NULL;
        xml::lite::MinidomParser domParser;

        size_t numSIDD = 0;
        const std::vector< csm::Des>& desList(isd.fileDess());
        for (size_t ii = 0; ii < desList.size(); ++ii)
        {
            const std::string& desData(desList[ii].data());

            if (!desData.empty())
            {
                try
                {
                    io::StringStream stream;
                    stream.write(desData.c_str(), desData.length());

                    domParser.clear();
                    domParser.parse(stream);

                    if (domParser.getDocument()->getRootElement()->getLocalName()
                            == "SIDD")
                    {
                        if (numSIDD == imageIndex)
                        {
                            siddXML = domParser.getDocument();
                            break;
                        }
                        ++numSIDD;
                    }
                }
                catch(const except::Exception& )
                {
                    // Couldn't parse DES as xml -- it's not a sidd so skip it
                }
            }
        }

        if (siddXML == NULL)
        {
            const std::string message = (numSIDD == 0) ? "Not a SIDD" :
                    "Found " + str::toString(numSIDD) +
                    " SIDD XMLs but requested image index " +
                    str::toString(imageIndex);

            throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                               message,
                               "SIDDSensorModel::SIDDSensorModel");
        }

        // get xml as string for sensor model state
        io::StringStream stringStream;
        siddXML->getRootElement()->print(stringStream);
        mSensorModelState = NAME + std::string(" ") + stringStream.stream().str();

        six::XMLControlRegistry xmlRegistry;
        xmlRegistry.addCreator(six::DataType::DERIVED,
                new six::XMLControlCreatorT<six::sidd::DerivedXMLControl>());

        logging::NullLogger logger;
        std::auto_ptr<six::XMLControl> control(
                xmlRegistry.newXMLControl(six::DataType::DERIVED, &logger));

        mData.reset(reinterpret_cast<six::sidd::DerivedData*>(control->fromXML(
                siddXML, mSchemaDirs)));
        reinitialize();
    }
    catch (const except::Exception& ex)
    {
        throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                           ex.getMessage(),
                           "SIDDSensorModel::initializeFromISD");
    }
}

bool SIDDSensorModel::containsDerivedDES(const csm::Nitf21Isd& isd)
{
    xml::lite::MinidomParser domParser;

    const std::vector< csm::Des>& desList(isd.fileDess());
    for (size_t ii = 0; ii < desList.size(); ++ii)
    {
        const std::string& desData(desList[ii].data());

        if (!desData.empty())
        {
            try
            {
                io::StringStream stream;
                stream.write(desData.c_str(), desData.length());

                domParser.clear();
                domParser.parse(stream);

                if (domParser.getDocument()->getRootElement()->getLocalName()
                        == "SIDD")
                {
                    return true;
                }
            }
            catch(const except::Exception& )
            {
                // Couldn't parse DES as xml -- it's not a sidd so skip it
            }
        }
    }

    return false;
}

csm::Version SIDDSensorModel::getVersion() const
{
    return VERSION;
}

std::string SIDDSensorModel::getModelName() const
{
    return NAME;
}

std::string SIDDSensorModel::getPedigree() const
{
    return (mData->getSource() + "_" + NAME + "_SAR");
}

std::string SIDDSensorModel::getImageIdentifier() const
{
    return mData->getName();
}

void SIDDSensorModel::setImageIdentifier(const std::string& imageId,
                                         csm::WarningList* )

{
    mData->setName(imageId);
}

std::string SIDDSensorModel::getSensorIdentifier() const
{
    return mData->getSource();
}

std::string SIDDSensorModel::getPlatformIdentifier() const
{
    return mData->getSource();
}

std::string SIDDSensorModel::getCollectionIdentifier() const
{
    // TODO: If there's more than one collection, what should we use?
    return mData->exploitationFeatures->collections[0]->identifier;
}

std::string SIDDSensorModel::getSensorMode() const
{
    // TODO: If there's more than one collection, what should we use?
    switch (mData->exploitationFeatures->collections[0]->information->radarMode)
    {
    case six::RadarModeType::SPOTLIGHT:
        return CSM_SENSOR_MODE_SPOT;
    case six::RadarModeType::STRIPMAP:
    case six::RadarModeType::DYNAMIC_STRIPMAP:
        return CSM_SENSOR_MODE_STRIP;
    default:
        return CSM_SENSOR_MODE_FRAME;
    }
}

std::string SIDDSensorModel::getReferenceDateAndTime() const
{
    // TODO: If there's more than one collection, what should we use?
    return mData->exploitationFeatures->collections[0]->information->
            collectionDateTime.format("%Y%m%dT%H%M%.2SZ");
}

six::DateTime SIDDSensorModel::getSIXDateTime() const
{
	return mData->exploitationFeatures->collections[0]->information->
            collectionDateTime;
}

types::RowCol<double>
SIDDSensorModel::fromPixel(const csm::ImageCoord& pos) const
{
    const types::RowCol<double> posRC(pos.line, pos.samp);
    types::RowCol<double> fullScenePos;
    if (mData->downstreamReprocessing.get() &&
        mData->downstreamReprocessing->geometricChip.get())
    {
        fullScenePos = mData->downstreamReprocessing->geometricChip->
                getFullImageCoordinateFromChip(posRC);
    }
    else
    {
        fullScenePos = posRC;
    }

    const six::sidd::MeasurableProjection* projection(reinterpret_cast<six::sidd::MeasurableProjection*>(
    		                    mData->measurement->projection.get()));
    const types::RowCol<double> ctrPt = projection->referencePoint.rowCol;

    return types::RowCol<double>(
            (fullScenePos.row - ctrPt.row) * projection->sampleSpacing.row,
            (fullScenePos.col - ctrPt.col) * projection->sampleSpacing.col);
}

types::RowCol<double>
SIDDSensorModel::toPixel(const types::RowCol<double>& pos) const
{
	//const types::RowCol<double> posRC(pos);
	types::RowCol<double> fullScenePos(pos);


	const six::sidd::MeasurableProjection* projection(reinterpret_cast<six::sidd::MeasurableProjection*>(
			mData->measurement->projection.get()));
	const types::RowCol<double> ctrPt = projection->referencePoint.rowCol;

	if (mData->downstreamReprocessing.get() &&
			mData->downstreamReprocessing->geometricChip.get())
	{
		// Not sure if the function getChipCoordinateFromFullImage does what I think it does
		return mData->downstreamReprocessing->geometricChip->getChipCoordinateFromFullImage(types::RowCol<double>(
				(fullScenePos.row / projection->sampleSpacing.row) + ctrPt.row,
				(fullScenePos.col / projection->sampleSpacing.col) + ctrPt.col));
	}
	else
	{
		return types::RowCol<double>(
				(fullScenePos.row / projection->sampleSpacing.row) + ctrPt.row,
				(fullScenePos.col / projection->sampleSpacing.col) + ctrPt.col);
	}

}

//types::RowCol<double>
//SIDDSensorModel::ecefToRowCol(const scene::Vector3& ecef) const
//{
//    const types::RowCol<double> imagePt = mGridTransform->ecefToRowCol(ecef);
//
//    if (mData->downstreamReprocessing.get() &&
//        mData->downstreamReprocessing->geometricChip.get())
//    {
//        // 'imagePt' is with respect to the original full image, but we
//        // need it with respect to the chip that this SIDD actually represents
//        return mData->downstreamReprocessing->geometricChip->
//                        getChipCoordinateFromFullImage(imagePt);
//    }
//    else
//    {
//        return imagePt;
//    }
//}

//scene::Vector3
//SIDDSensorModel::rowColToECEF(const types::RowCol<double>& imagePt) const
//{
//    types::RowCol<double> fullImagePt;
//    if (mData->downstreamReprocessing.get() &&
//        mData->downstreamReprocessing->geometricChip.get())
//    {
//        // The point that was passed in was with respect to the chip
//        // mGridTransform wants it with respect to the full image
//        fullImagePt = mData->downstreamReprocessing->geometricChip->
//                getFullImageCoordinateFromChip(imagePt);
//    }
//    else
//    {
//        fullImagePt = imagePt;
//    }
//
//    return mGridTransform->rowColToECEF(fullImagePt);
//}

csm::ImageVector SIDDSensorModel::getImageSize() const
{
    return csm::ImageVector(mData->getNumRows(), mData->getNumCols());
}

void SIDDSensorModel::replaceModelStateImpl(const std::string& sensorModelState)
{
    const size_t idx = sensorModelState.find(' ');
    if (idx == std::string::npos)
    {
        throw csm::Error(csm::Error::INVALID_SENSOR_MODEL_STATE,
                           "Invalid sensor model state",
                           "SIDDSensorModel::replaceModelStateImpl");
    }

    const std::string sensorModelName = sensorModelState.substr(0, idx);
    if (sensorModelName != NAME)
    {
        throw csm::Error(csm::Error::INVALID_SENSOR_MODEL_STATE,
                           "Invalid sensor model state",
                           "SIDDSensorModel::replaceModelStateImpl");
    }

    const std::string sensorModelXML = sensorModelState.substr(idx + 1);

    try
    {
        io::StringStream stream;
        stream.write(sensorModelXML.c_str(), sensorModelXML.length());

        xml::lite::MinidomParser domParser;
        domParser.parse(stream);

        six::XMLControlRegistry xmlRegistry;
        xmlRegistry.addCreator(six::DataType::DERIVED,
                new six::XMLControlCreatorT<six::sidd::DerivedXMLControl>());

        logging::NullLogger logger;
        std::auto_ptr<six::XMLControl> control(xmlRegistry.newXMLControl(
                six::DataType::DERIVED, &logger));

        // get xml as string for sensor model state
        mSensorModelState = sensorModelState;

        mData.reset(reinterpret_cast<six::sidd::DerivedData*>(control->fromXML(
                domParser.getDocument(), mSchemaDirs)));
        reinitialize();
    }
    catch (const except::Exception& ex)
    {
        throw csm::Error(csm::Error::INVALID_SENSOR_MODEL_STATE,
                           ex.getMessage(),
                           "SIDDSensorModel::replaceModelStateImpl");
    }
}

//const six::sidd::MeasurableProjection* SIDDSensorModel::getProjection() const
//{
//    if (!mData->measurement->projection->isMeasurable())
//    {
//        throw csm::Error(csm::Error::UNKNOWN_ERROR,
//                           "Image projection type is not measurable",
//                           "SIDDSensorModel::getProjection");
//    }
//
//    const six::sidd::MeasurableProjection* const projection =
//            reinterpret_cast<six::sidd::MeasurableProjection*>(
//                    mData->measurement->projection.get());
//    return projection;
//}

void SIDDSensorModel::reinitialize()
{
    // Sun Studio compiler can't assign non-const auto_ptr to const
    // auto_ptr, so need to release the other auto_ptr and reset our member
    // variable with the raw pointer
	mGeometry = six::sidd::Utilities::getSceneGeometry(mData.get());

	mProjection.reset(six::sidd::Utilities::getProjectionModel(mData.get()));
    std::fill_n(mAdjustableTypes,
                static_cast<size_t>(scene::AdjustableParams::NUM_PARAMS),
                csm::param::REAL);

    // NOTE: See member variable definition in header for why we're doing this
    mSensorCovariance = mProjection->getErrorCovariance(
            mGeometry->getReferencePosition());
}

types::RowCol<double> SIDDSensorModel::getSampleSpacing() const
{
			const six::sidd::MeasurableProjection* p =
                        reinterpret_cast<const six::sidd::MeasurableProjection*>(
                                mData->measurement->projection.get());
            return types::RowCol<double>(p->sampleSpacing);
}
}
}


