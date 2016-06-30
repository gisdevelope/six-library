/* =========================================================================
 * This file is part of six.sicd-c++
 * =========================================================================
 *
 * (C) Copyright 2004 - 2014, MDA Information Systems LLC
 *
 * six.sicd-c++ is free software; you can redistribute it and/or modify
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
 * License along with this program; If not,
 * see <http://www.gnu.org/licenses/>.
 *
 */
#include "six/sicd/CollectionInformation.h"
#include "six/sicd/GeoData.h"
#include "six/sicd/Grid.h"
#include "six/sicd/ImageData.h"
#include "six/sicd/PFA.h"
#include "six/sicd/SCPCOA.h"
#include "six/sicd/RadarCollection.h"
#include "six/sicd/RgAzComp.h"
#include "six/sicd/RMA.h"
#include "six/sicd/Utilities.h"

using namespace six;
using namespace six::sicd;

WeightType::WeightType() :
    windowName(Init::undefined<std::string>())
{
}

DirectionParameters::DirectionParameters() :
    unitVector(Init::undefined<Vector3>()),
    sampleSpacing(Init::undefined<double>()),
    impulseResponseWidth(Init::undefined<double>()),
    sign(Init::undefined<FFTSign>()),
    impulseResponseBandwidth(Init::undefined<double>()),
    kCenter(Init::undefined<double>()),
    deltaK1(Init::undefined<double>()),
    deltaK2(Init::undefined<double>()),
    deltaKCOAPoly(Init::undefined<Poly2D>())
{
}

DirectionParameters* DirectionParameters::clone() const 
{
    return new DirectionParameters(*this);
}

bool DirectionParameters::operator==(const DirectionParameters& rhs) const
{
    return (unitVector == rhs.unitVector &&
        sampleSpacing == rhs.sampleSpacing &&
        impulseResponseWidth == rhs.impulseResponseWidth &&
        sign == rhs.sign &&
        impulseResponseBandwidth == rhs.impulseResponseBandwidth &&
        kCenter == rhs.kCenter &&
        deltaK1 == rhs.deltaK1 &&
        deltaK2 == rhs.deltaK2 &&
        deltaKCOAPoly == rhs.deltaKCOAPoly &&
        weightType == rhs.weightType &&
        weights == rhs.weights);
}

std::vector<double> DirectionParameters::calculateDeltaKs(const ImageData& imageData) const
{
    //Here, we assume the min and max of DeltaKCOAPoly must be
    // on the vertices of the image, since it is smooth and monotonic in most cases--
    // although in actuality this is not always the case.  To be totally generic, 
    // we would have to search for an interior min and max as well
    double derivedDeltaK1 = 0;
    double derivedDeltaK2 = 0;

    std::vector<std::vector<sys::SSize_T> > vertices = calculateImageVertices(imageData);

    if (!Init::isUndefined<Poly2D>(deltaKCOAPoly))
    {
        derivedDeltaK1 = std::numeric_limits<double>::infinity();
        derivedDeltaK2 = -std::numeric_limits<double>::infinity();

        for (size_t ii = 0; ii < vertices[0].size(); ++ii)
        {
            double currentDeltaK = deltaKCOAPoly.atY(
                    static_cast<double>(vertices[1][ii]))(
                    static_cast<double>(vertices[0][ii]));
            derivedDeltaK1 = std::min(currentDeltaK, derivedDeltaK1);
            derivedDeltaK2 = std::max(currentDeltaK, derivedDeltaK2);
        }
    }

    derivedDeltaK1 -= (impulseResponseBandwidth / 2);
    derivedDeltaK2 += (impulseResponseBandwidth / 2);

    if (derivedDeltaK1 < -(1 / sampleSpacing) / 2 ||
            derivedDeltaK2 > (1 / sampleSpacing) / 2)
    {
        derivedDeltaK1 = -(1 / sampleSpacing) / 2;
        derivedDeltaK2 = -derivedDeltaK1;
    }

    std::vector<double> deltaKs;
    deltaKs.resize(2);
    deltaKs[0] = derivedDeltaK1;
    deltaKs[1] = derivedDeltaK2;
    return deltaKs;
}

std::auto_ptr<Functor>
DirectionParameters::calculateWeightFunction() const
{
    std::auto_ptr<Functor> weightFunction;
    bool useWeightFunction = false;

    if (weightType.get() != NULL)
    {
        std::string windowName(weightType->windowName);
        str::upper(windowName);

        if (windowName == "UNIFORM")
        {
            weightFunction.reset(new Identity());
        }
        else if (windowName == "HAMMING")
        {
            double coef;
            if (weightType->parameters.empty() || weightType->parameters[0].str().empty())
            {
                //A Hamming window is defined in many places as a raised cosine of weight .54,
                //so this is the default. However, some data use a generalized raised cosine and
                //call it HAMMING, so we allow for both uses.
                coef = .54;
            }
            else
            {
                coef = str::toType<double>(weightType->parameters[0].str());
            }

            weightFunction.reset(new RaisedCos(coef));
        }
        else if (windowName == "HANNING")
        {
            weightFunction.reset(new RaisedCos(0.50));
        }
        else if (windowName == "KAISER")
        {
            weightFunction.reset(new Kaiser(str::toType<double>(
                    weightType->parameters[0].str())));
        }
        else
        {
            //TODO: windowName == "TAYLOR"
            useWeightFunction = true;
        }
    }
    else
    {
        useWeightFunction = true;
    }

    if (useWeightFunction)
    {
        if (weights.empty())
        {
            weightFunction.reset();
            return weightFunction;
        }
        else
        {
            //TODO: interpft(weightFunction)
        }
    }
    return weightFunction;
}

std::vector<std::vector<sys::SSize_T> >
DirectionParameters::calculateImageVertices(const ImageData& imageData) const
{
    std::vector<std::vector<sys::SSize_T> > vertices;
    vertices.resize(2);

    if (imageData.validData.size() != 0)
    {
        //test vertices
        for (size_t ii = 0; ii < imageData.validData.size(); ++ii)
        {
            vertices[0].push_back(imageData.validData[ii].col);
        }
        for (size_t ii = 0; ii < imageData.validData.size(); ++ii)
        {
            vertices[1].push_back(imageData.validData[ii].row);
        }
    }
    else
    {
        //use edges of full image
        vertices[0].push_back(0);
        vertices[0].push_back(imageData.numCols - 1);
        vertices[0].push_back(imageData.numCols - 1);
        vertices[0].push_back(0);
        vertices[1].push_back(0);
        vertices[1].push_back(0);
        vertices[1].push_back(imageData.numRows - 1);
        vertices[1].push_back(imageData.numRows - 1);
    }
    return vertices;
}

void DirectionParameters::fillDerivedFields(const ImageData& imageData)
{
    // Calulating resolution requires fzero and fft functions

    // DeltaK1/2 are approximated from DeltaKCOAPoly
    if (!Init::isUndefined(deltaKCOAPoly) &&
        !Init::isUndefined(impulseResponseBandwidth) &&
        !Init::isUndefined(sampleSpacing) &&
        Init::isUndefined(deltaK1) &&
        Init::isUndefined(deltaK2))
    {
        // Here, we assume the min and max of DeltaKCOAPoly must be on the vertices
        // of the image, since it is smooth and monotonic in most cases--although in
        // actuality this is not always the case. To be totally generic, we would 
        // have to search for an interior min and max as well.

        std::vector<double> deltas = calculateDeltaKs(imageData);

        deltaK1 = deltas[0];
        deltaK2 = deltas[1];
    }

    if (weightType.get() != NULL &&
        weights.empty() &&
        weightType->windowName != "UNKNOWN")
    {
        size_t defaultWgtSize = 512;
        weights = (*calculateWeightFunction())(defaultWgtSize);
    }
    return;
}

void DirectionParameters::fillDerivedFields(const RgAzComp& rgAzComp,
        const GeoData& geoData, double offset)
{
    const Vector3& scp = geoData.scp.ecf;
    if (Init::isUndefined<double>(kCenter))
    {
        kCenter = derivedKCenter(rgAzComp, scp, offset);
    }

    if (Init::isUndefined<Poly2D>(deltaKCOAPoly) &&
        !Init::isUndefined<double>(kCenter))
    {
        deltaKCOAPoly = derivedKcoaPoly(rgAzComp, offset);
    }
}

double DirectionParameters::derivedKCenter(const RgAzComp& rgAzComp,
        const Vector3& scp, double offset) const
{
    (void)rgAzComp;
    double derivedCenter = offset;
    if (!Init::isUndefined<Poly2D>(deltaKCOAPoly))
    {
        // DeltaKCOAPoly populated, but not KCtr (would be odd)
        derivedCenter -= deltaKCOAPoly[0][0];
    }
    return derivedCenter;

}

Poly2D DirectionParameters::derivedKcoaPoly(const RgAzComp& rgAzComp,
        double offset) const
{
    (void)rgAzComp;
    // KCtr popualted, but not DeltaKCOAPoly
    // Create a Poly2D with one term
    std::vector<double> coefs(1, offset - kCenter);
    return Poly2D(0, 0, coefs);

}

bool DirectionParameters::validate(const ImageData& imageData,
        logging::Logger& log) const
{
    bool valid = true;
    std::ostringstream messageBuilder;
    const double& epsilon = std::numeric_limits<double>::epsilon();
    //2.3.1, 2.3.5
    if (deltaK2 <= deltaK1)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "SICD.Grid.Row/Col.DeltaK1: " << deltaK1 << std::endl
            << "SICD.Grid.Row/Col.DetalK2: " << deltaK2 << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }

    else
    {
        // 2.3.2, 2.3.6
        if (deltaK2 > (1 / (2 * sampleSpacing))
                + epsilon)
        {
            messageBuilder.str("");
            messageBuilder << boundsErrorMessage << std::endl
                << "0.5/SICD.Grid.Row/Col.SampleSpacing: " <<
                0.5 / sampleSpacing << std::endl
                << "SICD.Grid.Row/Col.DetalK2: " << deltaK2 << std::endl;
            log.error(messageBuilder.str());
            valid = false;
        }

        // 2.3.3, 2.3.7
        if (deltaK1 < (-1 / (2 * sampleSpacing))
                - epsilon)
        {
            messageBuilder.str("");
            messageBuilder << boundsErrorMessage << std::endl
                << "0.5/SICD.Grid.Row/Col.SampleSpacing: " <<
                0.5 / sampleSpacing << std::endl
                << "SICD.Grid.Row/Col.DetalK1: " << deltaK1 << std::endl;
            log.error(messageBuilder.str());
            valid = false;
        }

        // 2.3.4, 2.3.8
        if (impulseResponseBandwidth > (deltaK2 - deltaK1) + epsilon)
        {
            messageBuilder.str("");
            messageBuilder << boundsErrorMessage << std::endl
                << "SICD.Grid.Row/Col.impulseResponseBandwidth: " <<
                    impulseResponseBandwidth << std::endl
                << "SICD.Grid.Row/Col.DeltaK2 - SICD.Grid.Row/COl.DeltaK1: "
                << deltaK2 - deltaK1 << std::endl;
            log.error(messageBuilder.str());
            valid = false;
        }
    }

    // 2.3.9. Compute our own DeltaK1/K2 and test for consistency with DelaKCOAPoly,
    // ImpRespBW, and SS.
    std::vector<double> deltas = calculateDeltaKs(imageData);

    const double minDk = deltas[0];
    const double maxDk = deltas[1];

    const double DK_TOL = 1e-2;

    //2.3.9.1, 2.3.9.3
    if (std::abs((deltaK1 / minDk) - 1)
            > DK_TOL)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "SICD.Grid.Row/Col.DeltaK1: " << deltaK1 << std::endl
            << "Derived DeltaK1: " << minDk << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }
    //2.3.9.2, 2.3.9.4
    if (std::abs((deltaK2 / maxDk) - 1)
            > DK_TOL)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "SICD.Grid.Row/Col.DeltaK2: " << deltaK2 << std::endl
            << "Derived DeltaK2: " << maxDk << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }

    // Check weight functions
    //std::vector<double> expectedWeights; <- Used in unimplemented section
    std::auto_ptr<Functor> weightFunction;
    const size_t DEFAULT_WGT_SIZE = 512;

    if (weightType.get())
    {
        weightFunction.reset(calculateWeightFunction().release());

        if (weightFunction.get())
        {
            if (!weights.empty())
            {
                valid = valid && validateWeights(*weightFunction, log);
            }
            else
            {
                //expectedWeights = (*weightFunction)(DEFAULT_WGT_SIZE);
            }
        }
        else
        {
            messageBuilder.str("");
            messageBuilder << "Unrecognized weighting description" << std::endl
                << "WeightType.WindowName: "
                << weightType->windowName << std::endl;
            log.warn(messageBuilder.str());
            valid = false;
        }
    }

    // 2.4.3, 2.4.4
    if (weightType.get() &&
        weightType->windowName != "UNIFORM" &&
        weightType->windowName != "UNKNOWN" &&
        weights.empty())
    {
        messageBuilder.str("");
        messageBuilder << "Non-uniform weighting, but no WgtFunct provided"
            << std::endl << "WgtType.WindowName: " << weightType->windowName
            << std::endl;
        log.warn(messageBuilder.str());
    }

    // TODO: 2.5 (requires fzero)

    return valid;
}

bool DirectionParameters::validateWeights(const Functor& weightFunction,
        logging::Logger& log) const
{
    bool consistentValues = true;
    bool valid = true;
    std::ostringstream messageBuilder;

    //Arg doesn't matter. Just checking for Uniform-type Functor
    if (weightFunction(5).empty())
    {
        double key = weights[0];
        for (size_t ii = 0; ii < weights.size(); ++ii)
        {
            if (key != weights[ii])
            {
                consistentValues = false;
            }
        }
    }
    else
    {
        std::vector<double> expectedWeights = weightFunction(weights.size());
        for (size_t ii = 0; ii < weights.size(); ++ii)
        {
            if (std::abs(expectedWeights[ii] - weights[ii]) > WGT_TOL)
            {
                consistentValues = false;
                break;
            }
        }
    }

    if (!consistentValues)
    {
        messageBuilder.str("");
        messageBuilder << "DirectionParameters weights values "
            << "inconsistent with weightType" << std::endl
            << "WeightType.WindowName: "
            << weightType->windowName << std::endl;
        log.warn(messageBuilder.str());
        valid = false;
    }

    return valid;
}

bool DirectionParameters::validate(const RgAzComp& rgAzComp,
        const Vector3& scp,
        logging::Logger& log,
        double offset) const
{
    bool valid = true;
    std::ostringstream messageBuilder;

    // 2.12.1.8, 2.12.1.9
    if (std::abs(kCenter - derivedKCenter(rgAzComp, scp, offset))
            > std::numeric_limits<double>::epsilon())
    {
        messageBuilder.str("");
        messageBuilder << "KCenter: " << kCenter << std::endl
            << "DeltaKCOAPoly: " << deltaKCOAPoly[0][0];
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.12.1.10, 2.12.1.11
    if (!Init::isUndefined(deltaKCOAPoly) && deltaKCOAPoly.orderX() > 1)
    {
        messageBuilder.str("");
        messageBuilder << "DetlaKCOAPoly must be a single value for RGAZCOMP data";
        log.error(messageBuilder.str());
        valid = false;
    }
    return valid;
}

Grid::Grid() :
    // This is a good assumption, I think
    imagePlane(ComplexImagePlaneType::SLANT),
    // Not so sure about this one
    type(ComplexImageGridType::RGAZIM),
    row(new DirectionParameters()),
    col(new DirectionParameters())
{
}

Grid* Grid::clone() const 
{
    return new Grid(*this);
}

bool Grid::operator==(const Grid& rhs) const
{
    return (imagePlane == rhs.imagePlane &&
        type == rhs.type &&
        timeCOAPoly == rhs.timeCOAPoly &&
        row == rhs.row && col == rhs.col);
}

bool Grid::validateTimeCOAPoly(
        const CollectionInformation& collectionInformation,
        logging::Logger& log) const
{
    const RadarModeType& mode = collectionInformation.radarMode;

    //2.1. Scalar TimeCOAPoly means SPOTLIGHT data
    bool isScalar = true;
    bool valid = true;

    // I don't know that it's impossible for a one-degree polynomial to be expressed
    // as a polynomial of higher order for whatever reason, so I'm checking each term
    // manually
    for (size_t ii = 0; ii <= timeCOAPoly.orderX(); ++ii)
    {
        for (size_t jj = 0; jj <= timeCOAPoly.orderY(); ++jj)
        {
            if (ii == 0 && jj == 0)
            {
                // Don't care what the (0,0) value is, but everything else
                // should be 0.
                continue;
            }
            if (timeCOAPoly[ii][jj] != 0)
            {
                isScalar = false;
                break;
            }
        }
    }

    if (mode == RadarModeType::SPOTLIGHT && !isScalar)
    {
        log.error("SPOTLIGHT data should only have scalar TimeCOAPoly.");
        valid = false;
    }

    if (mode != RadarModeType::SPOTLIGHT && isScalar)
    {
        log.warn("Non-SPOTLIGHT data will generally have more than one nonzero"
            "term in TimeCOAPoly unless \"formed as spotlight\".");
        valid = false;
    }

    return valid;
}

bool Grid::validateFFTSigns(logging::Logger& log) const
{
    bool valid = true;
    std::ostringstream messageBuilder;

    //2.2. FFT signs in both dimensions almost certainly have to be equal
    if (row->sign != col->sign)
    {
        messageBuilder.str("");
        messageBuilder <<
            "FFT signs in row and column direction should be the same." <<
            std::endl << "Grid.Row.Sign: " << row->sign.toString() << std::endl
            << "Grid.Col.Sign: " << col->sign.toString() << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }
    return valid;
}

bool Grid::validate(const CollectionInformation& collectionInformation,
        const ImageData& imageData,
        logging::Logger& log) const
{
    return (validateTimeCOAPoly(collectionInformation, log) &&  //2.1
        validateFFTSigns(log) &&                                //2.2
        row->validate(imageData, log) &&
        col->validate(imageData, log)                           //2.3.1 - 2.3.9
        );
}

void Grid::fillDerivedFields(
        const CollectionInformation& collectionInformation,
        const ImageData& imageData,
        const SCPCOA& scpcoa)
{
    if (!Init::isUndefined<double>(scpcoa.scpTime) &&
        collectionInformation.radarMode == RadarModeType::SPOTLIGHT)
    {
        if (Init::isUndefined<Poly2D>(timeCOAPoly))
        {
            timeCOAPoly = Poly2D(1, 1);
            timeCOAPoly[0][0] = scpcoa.scpTime;
        }
    }

    row->fillDerivedFields(imageData);
    col->fillDerivedFields(imageData);
}

void Grid::fillDerivedFields(const RMA& rma, const Vector3& scp,
        const PolyXYZ& arpPoly)
{
    if (rma.rmat.get())
    {
        fillDerivedFields(*rma.rmat, scp);
    }
    else if (rma.rmcr.get())
    {
        fillDerivedFields(*rma.rmcr, scp);
    }
    else if (rma.inca.get())
    {
        fillDerivedFields(*rma.inca, scp, arpPoly);
    }
}

void Grid::fillDerivedFields(const RMAT& rmat, const Vector3& scp)
{
    // Row/Col.UnitVector and Derived fields
    if (Init::isUndefined<Vector3>(row->unitVector) &&
        Init::isUndefined<Vector3>(col->unitVector))
    {
        row->unitVector = rmat.uXCT(scp);
        col->unitVector = rmat.uYAT(scp);
    }
}

void Grid::fillDerivedFields(const RMCR& rmcr, const Vector3& scp)
{
    // Row/Col.UnitVector and Derived fields
    if (Init::isUndefined<Vector3>(row->unitVector) &&
        Init::isUndefined<Vector3>(col->unitVector))
    {
        row->unitVector = rmcr.uXRG(scp);
        col->unitVector = rmcr.uYCR(scp);
    }
}

void Grid::fillDerivedFields(const INCA& inca, const Vector3& scp,
        const PolyXYZ& arpPoly)
{

    if (!Init::isUndefined<Poly1D>(inca.timeCAPoly) &&
        !Init::isUndefined<PolyXYZ>(arpPoly) &&
        Init::isUndefined<Vector3>(row->unitVector) &&
        Init::isUndefined<Vector3>(col->unitVector))
    {
        row->unitVector = inca.uRG(scp, arpPoly);
        col->unitVector = inca.uAZ(scp, arpPoly);
    }

    if (Init::isUndefined<double>(col->kCenter))
    {
        col->kCenter = 0;
    }

    if (!Init::isUndefined<double>(inca.freqZero) &&
        Init::isUndefined<double>(row->kCenter))
    {
        row->kCenter = derivedRowKCenter(inca);
    }
}

void Grid::fillDerivedFields(const RgAzComp& rgAzComp,
        const GeoData& geoData,
        const SCPCOA& scpcoa,
        double fc)
{
    const Vector3& scp = geoData.scp.ecf;

    if (imagePlane == ComplexImagePlaneType::NOT_SET)
    {
        imagePlane = ComplexImagePlaneType::SLANT;
    }
    if (imagePlane == ComplexImageGridType::NOT_SET)
    {
        type = ComplexImageGridType::RGAZIM;
    }

    if (Init::isUndefined<Vector3>(row->unitVector))
    {
        row->unitVector = derivedRowUnitVector(scpcoa, scp);
    }
    if (Init::isUndefined<Vector3>(col->unitVector))
    {
        col->unitVector = derivedColUnitVector(scpcoa, scp);
    }

    row->fillDerivedFields(rgAzComp, geoData,
            fc * 2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC);
    col->fillDerivedFields(rgAzComp, geoData, 0);
}

Vector3 Grid::derivedRowUnitVector(
        const SCPCOA& scpcoa, const Vector3& scp) const
{
    return scpcoa.uLOS(scp);
}

Vector3 Grid::derivedColUnitVector(
    const SCPCOA& scpcoa, const Vector3& scp) const
{
    return cross(scpcoa.slantPlaneNormal(scp), scpcoa.uLOS(scp));
}

double Grid::derivedRowKCenter(const INCA& inca) const
{
    return inca.freqZero * 2 /
        math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC;
}

void Grid::fillDefaultFields(const RMA& rma, double fc)
{
    if (imagePlane == ComplexImagePlaneType::NOT_SET)
    {
        imagePlane = defaultPlaneType(rma);
    }
    if (type == ComplexImageGridType::NOT_SET)
    {
        type = defaultGridType(rma);
    }
    if (rma.rmat.get())
    {
        fillDefaultFields(*rma.rmat, fc);
    }
    else if (rma.rmcr.get())
    {
        fillDefaultFields(*rma.rmcr, fc);
    }
}

void Grid::fillDefaultFields(const RMAT& rmat, double fc)
{
    if (!Init::isUndefined<double>(fc))
    {
        if (Init::isUndefined<double>(row->kCenter))
        {
            row->kCenter = derivedRowKCenter(rmat, fc);
        }

        if (Init::isUndefined<double>(col->kCenter))
        {
            col->kCenter = derivedColKCenter(rmat, fc);
        }
    }
}

double Grid::derivedRowKCenter(const RMAT& rmat, double fc) const
{
    const double kfc = fc * 2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC;
    return kfc * std::sin(rmat.dopConeAngleRef *
            math::Constants::DEGREES_TO_RADIANS);
}

double Grid::derivedColKCenter(const RMAT& rmat, double fc) const
{
    const double kfc = fc * 2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC;
    return kfc * std::cos(rmat.dopConeAngleRef *
        math::Constants::DEGREES_TO_RADIANS);
}

void Grid::fillDefaultFields(const RMCR& rmcr, double fc)
{
    if (!Init::isUndefined<double>(fc))
    {
        if (Init::isUndefined<double>(row->kCenter))
        {
            row->kCenter = derivedRowKCenter(rmcr, fc);
        }
        if (Init::isUndefined<double>(col->kCenter))
        {
            col->kCenter = 0;
        }
    }
}

void Grid::fillDefaultFields(const PFA& pfa, double fc)
{
    if (type == ComplexImageGridType::NOT_SET)
    {
        type = ComplexImageGridType::RGAZIM;
    }

    if (Init::isUndefined<double>(col->kCenter))
    {
        col->kCenter = 0;
    }
    if (Init::isUndefined<double>(row->kCenter))
    {
        if (!Init::isUndefined<double>(pfa.krg1) &&
            !Init::isUndefined<double>(pfa.krg2))
        {
            // Default: the most reasonable way to compute this
            row->kCenter = (pfa.krg1 + pfa.krg2) / 2;
        }
        else if (!Init::isUndefined<double>(fc))
        {
            // Approximation: this may not be quite right, due to
            // rectangular inscription loss in PFA, but it should
            // be close.
            row->kCenter = fc * (2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC) *
                pfa.spatialFrequencyScaleFactorPoly[0];
        }
    }
}

bool Grid::validate(const RMA& rma, const Vector3& scp,
        const PolyXYZ& arpPoly, double fc,
        logging::Logger& log) const
{
    bool valid = true;
    // 2.12.3.2.1, 2.12.3.4.1
    if (type != defaultGridType(rma))
    {
        std::ostringstream messageBuilder;
        messageBuilder << "Given image formation algorithm expects "
            << defaultGridType(rma).toString() << ".\nFound " << type;
        log.error(messageBuilder.str());
        valid = false;
    }

    if (rma.rmat.get())
    {
        return validate(*rma.rmat, scp, fc, log);
    }

    else if (rma.rmcr.get())
    {
        return validate(*rma.rmcr, scp, fc, log);
    }

    else if (rma.inca.get())
    {
        return validate(*rma.inca, scp, arpPoly, fc, log);
    }

    // If no image formation algorithm is present, the problem isn't
    // with the Grid, so we'll let RMA deal with it that error
    return valid;
}

bool Grid::validate(const RMAT& rmat, const Vector3& scp,
    double fc, logging::Logger& log) const
{
    (void)rmat; // only for overloading
    std::ostringstream messageBuilder;
    bool valid = true;

    // 2.12.3.2.3
    if ((row->unitVector - rmat.uXCT(scp)).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVect fields inconsistent." << std::endl
            << "Grid.Row.UVectECF: " << row->unitVector
            << "Derived grid.Row.UVectECT: " << rmat.uXCT(scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.2.4
    if ((col->unitVector - rmat.uYAT(scp)).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVect fields inconsistent." << std::endl
            << "Grid.Col.UVectECF: " << col->unitVector
            << "Derived Grid.Col.UVectECF: " << rmat.uYAT(scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.2.6
    if (std::abs(derivedRowKCenter(rmat, fc) / row->kCenter - 1) > WF_TOL)
    {
        messageBuilder.str("");
        messageBuilder << WF_INCONSISTENT_STR
            << "Grid.Row.KCtr: " << row->kCenter << std::endl
            << "Derived KCtr: " << derivedRowKCenter(rmat, fc);
        log.warn(messageBuilder.str());
        valid = false;
    }

    //2.12.3.2.7
    if (std::abs(derivedColKCenter(rmat, fc) / col->kCenter - 1) > WF_TOL)
    {
        messageBuilder.str("");
        messageBuilder << WF_INCONSISTENT_STR
            << "Grid.Col.KCtr: " << col->kCenter << std::endl
            << "Derived KCtr: " << derivedColKCenter(rmat, fc);
        log.warn(messageBuilder.str());
        valid = false;
    }

    return valid;
}

bool Grid::validate(const RMCR& rmcr, const Vector3& scp,
        double fc, logging::Logger& log) const
{
    (void)rmcr; // Only for overloading
    bool valid = true;
    std::ostringstream messageBuilder;

    //2.12.3.3.3
    if ((row->unitVector - rmcr.uXRG(scp)).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVect fields inconsistent." << std::endl
            << "Grid.Row.UVectECF: " << row->unitVector << std::endl
            << "Derived Grid.Row.UVectECF: " << rmcr.uXRG(scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.3.4
    if ((col->unitVector - rmcr.uYCR(scp)).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVect fields inconsistent." << std::endl
            << "Grid.Col.UVectECF: " << col->unitVector << std::endl
            << "Derived Grid.Col.UVectECF: " << rmcr.uYCR(scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.3.6
    if (col->kCenter != 0)
    {
        messageBuilder.str("");
        messageBuilder << "Grid.Col.KCtr must be zero for RMA/RMCR data." 
            << std::endl << "Grid.Col.KCtr = " << col->kCenter;
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.3.7
    if (!Init::isUndefined<double>(fc))
    {
        if (std::abs(row->kCenter / derivedRowKCenter(rmcr, fc) - 1) > WF_TOL)
        {
            messageBuilder.str("");
            messageBuilder << WF_INCONSISTENT_STR << std::endl
                << "Grid.Row.KCtr: " << row->kCenter << std::endl
                << "Center frequency * 2/c: " << derivedRowKCenter(rmcr, fc);
            log.warn(messageBuilder.str());
            valid = false;
        }
    }

    return valid;
}

double Grid::derivedRowKCenter(const RMCR& rmcr, double fc) const
{
    return fc * (2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC);
}

bool Grid::validate(const INCA& inca, const Vector3& scp,
        const PolyXYZ& arpPoly, double fc,
        logging::Logger& log) const
{
    (void)inca; // Only for overloading
    bool valid = true;
    std::ostringstream messageBuilder;
    const double IFP_POLY_TOL = 1e-5;

    if (!Init::isUndefined<Poly2D>(inca.dopplerCentroidPoly) &&
        inca.dopplerCentroidCOA == BooleanType::IS_TRUE)
    {
        const Poly2D& kcoaPoly = col->deltaKCOAPoly;
        const Poly2D& centroidPoly = inca.dopplerCentroidPoly;

        if (kcoaPoly.orderX() != centroidPoly.orderX() &&
            kcoaPoly.orderY() != centroidPoly.orderY())
        {
            valid = false;
            messageBuilder.str("");
            messageBuilder << "Grid.Col.delaKCOAPoly and "
                << "RMA.INCA.dopplerCentroidPoly have diferent sizes.";
            log.error(messageBuilder.str());
        }
        else
        {
            Poly2D differencePoly = kcoaPoly - (centroidPoly * inca.timeCAPoly[1]);
            double norm = 0;

            for (size_t ii = 0; ii < differencePoly.orderX(); ++ii)
            {
                for (size_t jj = 0; jj < differencePoly.orderY(); ++jj)
                {
                    norm += std::pow(differencePoly[ii][jj], 2);
                }
            }
            norm = std::sqrt(norm);

            if(norm > IFP_POLY_TOL)
            {
                messageBuilder.str("");
                messageBuilder << "RMA.INCA fields inconsistent." << std::endl
                    << "Compare Grid.Col.KCOAPoly to RMA.INCA.DopCentroidPoly "
                    << "* RMA.INCA.TimeCAPoly[1].";
                log.error(messageBuilder.str());
                valid = false;
            }
        }
    }

    // 2.12.3.4.6
    if ((inca.uRG(scp, arpPoly) - row->unitVector).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVectFields inconsistent" << std::endl
            << "Grid.Row.UVectECF: " << row->unitVector
            << "Derived Grid.Row.UVectECF: " << inca.uRG(scp, arpPoly);
        log.error(messageBuilder.str());
        valid =  false;
    }

    // 2.12.3.4.7
    if ((inca.uAZ(scp, arpPoly) - col->unitVector).norm() > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVectFields inconsistent" << std::endl
            << "Grid.Col.UVectECF: " << col->unitVector
            << "Derived Grid.Col.UVectECF: " << inca.uAZ(scp, arpPoly);
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.4.8
    if (col->kCenter != 0)
    {
        messageBuilder.str("");
        messageBuilder << "Grid.Col.KCtr  must be zero "
            << "for RMA/INCA data." << std::endl
            << "Grid.Col.KCtr: " << col->kCenter;
        log.error(messageBuilder.str());
        valid = false;
    }

    // 2.12.3.4.11
    if (Init::isUndefined<double>(fc) &&
        std::abs(row->kCenter - derivedRowKCenter(inca)) >
        std::numeric_limits<double>::epsilon())
    {
        const std::string WF_INCONSISTENT_STR =
                "Waveform fields not consistent";
        messageBuilder.str("");
        messageBuilder << WF_INCONSISTENT_STR << std::endl
            << "RMA.INCA.FreqZero * 2 / c: " << derivedRowKCenter(inca)
            << "Grid.Row.KCenter: " << row->kCenter;
        log.error(messageBuilder.str());
        valid = false;
    }
    return valid;
}

bool Grid::validate(const PFA& pfa, const RadarCollection& radarCollection,
            double fc, logging::Logger& log) const
{
    bool valid = true;
    std::ostringstream messageBuilder;
    const double& epsilon = std::numeric_limits<double>::epsilon();

    //2.12.2.1
    if (type != ComplexImageGridType::RGAZIM)
    {
        messageBuilder.str("");
        messageBuilder << "PFA image formation should result in a RGAZIM grid."
            << std::endl << "Grid.Type: " << type.toString();
        log.error(messageBuilder.str());
        valid = false;
    }

    // Make sure Row.kCtr is consistent with processed RF frequency bandwidth
    if (Init::isUndefined<double>(radarCollection.refFrequencyIndex) &&
        !Init::isUndefined<double>(fc))
    {
        // PFA.SpatialFreqSFPoly affects Row.KCtr
        double kapCtr = fc * pfa.spatialFrequencyScaleFactorPoly[0] *
                2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC;

        // PFA inscription could cause kapCtr and Row.KCtr 
        // to be somewhat different
        double theta = std::atan((col->impulseResponseBandwidth / 2) /
                row->kCenter);
        double kCtrTol = 1 - std::cos(theta);
        kCtrTol = std::max(0.01, kCtrTol);

        if (std::abs(row->kCenter / kapCtr - 1) > kCtrTol)
        {
            messageBuilder.str("");
            messageBuilder << WF_INCONSISTENT_STR
                << "Grid.Row.KCtr: " << row->kCenter << std::endl
                << "Derived KapCtr: " << kapCtr;
            log.error(messageBuilder.str());
            valid = false;
        }
    }

    //Slow-time deskew would allow for PFA.Kaz2-PFA.Kaz1>(1/Grid.Col.SS),
    //since Kaz bandwidth is compressed from original polar annulus.
    if (pfa.slowTimeDeskew->applied != BooleanType::IS_TRUE)
    {
        //2.3.10
        if ((pfa.kaz2 - col->kCenter) >
            (1 / (2 * col->sampleSpacing)) + epsilon)
        {
            messageBuilder.str("");
            messageBuilder << boundsErrorMessage << std::endl
                << "0.5/SICD.Grid.Col.SampleSpacing: "
                << 0.5 / col->sampleSpacing << std::endl
                << "PFA.Kaz2 - Grid.Col.KCenter: "
                << pfa.kaz2 - col->kCenter << std::endl;
            log.error(messageBuilder.str());
            valid = false;
        }
        //2.3.11
        if ((pfa.kaz1 - col->kCenter) <
            (-1 / (2 * col->sampleSpacing)) - epsilon)
        {
            messageBuilder.str("");
            messageBuilder << boundsErrorMessage << std::endl
                << "0.5/SICD.Grid.Col.SampleSpacing: "
                << 0.5 / col->sampleSpacing << std::endl
                << "PFA.Kaz1 - Grid.Col.KCenter: "
                << pfa.kaz1 - col->kCenter << std::endl;
            log.error(messageBuilder.str());
            valid = false;
        }
    }

    //2.3.12
    if ((pfa.krg2 - row->kCenter) >
        (1 / (2 * row->sampleSpacing)) + epsilon)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "0.5/SICD.Grid.Row.SampleSpacing: "
            << 0.5 / row->sampleSpacing << std::endl
            << "PFA.Krg2 - Grid.Row.KCenter: "
            << pfa.krg2 - row->kCenter << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.3.13
    if (pfa.krg1 - row->kCenter <
        (-1 / (2 * row->sampleSpacing)) - epsilon)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "0.5/SICD.Grid.Row.SampleSpacing: "
            << 0.5 / row->sampleSpacing << std::endl
            << "PFA.Krg1 - Grid.Row.KCenter: "
            << pfa.krg1 - row->kCenter << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.3.14
    if (col->impulseResponseBandwidth > pfa.kaz2 - pfa.kaz1 + epsilon)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "Grid.Col.ImpulseResponseBandwidth: "
            << col->impulseResponseBandwidth << std::endl
            << "SICD.PFA.Kaz2 - SICD.PFA.Kaz1: "
            << pfa.kaz2 - pfa.kaz1 << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }
    //2.3.15
    if (row->impulseResponseBandwidth > pfa.krg2 - pfa.krg1 + epsilon)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "Grid.Row.ImpulseResponseBandwidth: "
            << row->impulseResponseBandwidth << std::endl
            << "SICD.PFA.Krg2 - SICD.PFA.Krg1: "
            << pfa.krg2 - pfa.krg1 << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }
    //2.3.16
    if (col->kCenter != 0 &&
        std::abs(col->kCenter - (pfa.kaz1 + pfa.kaz2) / 2) > 1e-5)
    {
        messageBuilder.str("");
        messageBuilder << boundsErrorMessage << std::endl
            << "Grid.Col.KCenter: " << col->kCenter << std::endl
            << "mean(SICD.PFA.Kaz1, SICD.PFA.Kaz2): "
            << (pfa.kaz1 + pfa.kaz2) / 2 << std::endl;
        log.error(messageBuilder.str());
        valid = false;
    }

    return valid;
}

bool Grid::validate(const RgAzComp& rgAzComp,
        const GeoData& geoData,
        const SCPCOA& scpcoa,
        double fc,
        logging::Logger& log) const
{
    bool valid = true;
    std::ostringstream messageBuilder;
    
    // 2.12.1.1
    if (imagePlane != ComplexImagePlaneType::SLANT)
    {
        messageBuilder.str("");
        messageBuilder << "RGAZCOMP image formation should result in a SLANT plane image." << std::endl
            << "Grid.ImagePlane: " << imagePlane.toString();
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.12.1.2
    if (type != ComplexImageGridType::RGAZIM)
    {
        messageBuilder.str("");
        messageBuilder << "RGAZCOMP image formation should result in a RGAZIM grid." << std::endl
            << "Grid.Type: " << type.toString();
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.12.1.8
    const Vector3& scp = geoData.scp.ecf;
    valid = valid && col->validate(rgAzComp, scp, log);
    valid = valid && row->validate(rgAzComp, scp, log,
            fc *(2 / math::Constants::SPEED_OF_LIGHT_METERS_PER_SEC));

    //2.12.1.6
    if ((derivedRowUnitVector(scpcoa, scp) - row->unitVector).norm()
            > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVect fields inconsistent." << std::endl
            << "Grid.Row.UVectECEF: " << row->unitVector << std::endl
            << "Derived Grid.Row.UVectECEF: " <<
            derivedRowUnitVector(scpcoa, scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    //2.12.1.7
    if ((derivedColUnitVector(scpcoa, scp) - col->unitVector).norm()
            > UVECT_TOL)
    {
        messageBuilder.str("");
        messageBuilder << "UVectr fields inconsistent." << std::endl
            << "Grid.Col.UVectECF: " << col->unitVector << std::endl
            << "Derived Grid.Col.UVectECF: " <<
            derivedColUnitVector(scpcoa, scp);
        log.error(messageBuilder.str());
        valid = false;
    }

    return valid;
}

ComplexImageGridType Grid::defaultGridType(const RMA& rma) const
{
    if (rma.rmat.get())
    {
        return ComplexImageGridType::XCTYAT;
    }
    else if (rma.rmcr.get())
    {
        return ComplexImageGridType::XRGYCR;
    }
    else if (rma.inca.get())
    {
        return ComplexImageGridType::RGZERO;
    }
    return ComplexImageGridType::NOT_SET;
}

ComplexImagePlaneType Grid::defaultPlaneType(const RMA& rma) const
{
    if (rma.rmat.get() || rma.rmcr.get())
    {
        return ComplexImagePlaneType::SLANT;
    }
    else if (rma.inca.get())
    {
        return this->imagePlane;
    }
    return ComplexImagePlaneType::NOT_SET;
}
