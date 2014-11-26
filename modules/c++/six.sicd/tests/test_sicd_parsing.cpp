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
 * Simple program to round-trip SICD XML and verify the resulting XML matches
 */
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/OS.h>
#include <sys/Path.h>
#include <except/Exception.h>
#include <six/sicd/ComplexXMLControl.h>
#include <six/Utilities.h>
#include <io/FileInputStream.h>
#include <logging/StreamHandler.h>

namespace
{
class XMLVerifier
{
public:
    XMLVerifier();

    void verify(const std::string& pathname) const;

private:
    void readFile(const std::string& pathname, std::string& contents) const;

private:
    six::XMLControlRegistry mXmlRegistry;
    const std::vector<std::string> mSchemaPaths;
    mutable logging::Logger mLog;

    mutable std::vector<char> mScratch;
};

XMLVerifier::XMLVerifier()
{
    // Verify schema path is set
    sys::OS os;

    try
    {
        os.getEnv(six::SCHEMA_PATH);
    }
    catch(const except::Exception& ex)
    {
        std::ostringstream oss;
        oss << "Must specify SICD/SIDD schema path via "
            << six::SCHEMA_PATH << " environment variable";
        throw except::Exception(Ctxt(oss.str()));
    }

    mXmlRegistry.addCreator(six::DataType::COMPLEX,
                            new six::XMLControlCreatorT<
                                    six::sicd::ComplexXMLControl>());

    mLog.addHandler(new logging::StreamHandler(logging::LogLevel::LOG_INFO),
                    true);
}

void XMLVerifier::readFile(const std::string& pathname,
                           std::string& contents) const
{
    sys::File inFile(pathname);
    mScratch.resize(inFile.length());

    if (mScratch.empty())
    {
        contents.clear();
    }
    else
    {
        inFile.readInto(&mScratch[0], mScratch.size());
        contents.assign(&mScratch[0], mScratch.size());
    }
}

void XMLVerifier::verify(const std::string& pathname) const
{
    std::cout << "Verifying " << pathname << "...";

    // Read the file in and save it off to a string for comparison later
    std::string inStr;
    readFile(pathname, inStr);

    // Parse the XML - this verifies both that the XML validates against
    // the schema and that our parser reads it without errors
    io::StringStream inStream;
    inStream.write(reinterpret_cast<const sys::byte*>(inStr.c_str()),
                   inStr.length());

    std::auto_ptr<six::Data> data(six::parseData(mXmlRegistry,
                                                 inStream,
                                                 six::DataType::COMPLEX,
                                                 mSchemaPaths,
                                                 mLog));

    // Write it back out - this verifies both that the XML we write validates
    // against the schema and that our parser writes it without errors
    six::sicd::ComplexXMLControl xmlControl;
    std::auto_ptr<xml::lite::Document> xmlDoc(xmlControl.toXML(data.get(),
                                              mSchemaPaths));

    io::StringStream outStream;
    xmlDoc->getRootElement()->prettyPrint(outStream);
    const std::string outStr(outStream.stream().str());

    // Now verify the two strings match
    // TODO: A better check would be if we added operator== to six::Data
    //       For now, assuming we pretty-printed the input XML and so this
    //       will work
    if (inStr != outStr)
    {
        throw except::Exception(Ctxt(
                "Round-tripped XML does not match for '" + pathname + "'"));
    }
    std::cout << " verified" << std::endl;
}
}

int main(int argc, char** argv)
{
    try
    {
        // Parse the command line
        if (argc < 2)
        {
            std::cerr << "Usage: " << sys::Path::basename(argv[0])
                      << " <SICD XML pathname #1>"
                      << " <SICD XML pathname #2> ...\n";
            return 1;
        }

        XMLVerifier verifier;
        for (int ii = 1; ii < argc; ++ii)
        {
            verifier.verify(argv[ii]);
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Caught std::exception: " << ex.what() << std::endl;
        return 1;
    }
    catch (const except::Exception& ex)
    {
        std::cerr << "Caught except::exception: " << ex.getMessage()
                  << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Caught unknown exception\n";
        return 1;
    }
}
