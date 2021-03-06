/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Net/Socket.h>
#include <Poco/Path.h>
#include <Poco/StringTokenizer.h>
#include <Poco/URI.h>
#include <cppunit/extensions/HelperMacros.h>
#include <Poco/JSON/JSON.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>
#include <LOOLProtocol.hpp>
#include <Common.hpp>
#include <ChildProcessSession.hpp>

/// Tests the HTTP WebSocket API of loolwsd. The server has to be started manually before running this test.
class HTTPWSTest : public CPPUNIT_NS::TestFixture
{
    const Poco::URI _uri;
    Poco::Net::HTTPClientSession _session;
    Poco::Net::HTTPRequest _request;
    Poco::Net::HTTPResponse _response;

    CPPUNIT_TEST_SUITE(HTTPWSTest);
    CPPUNIT_TEST(testPaste);
    CPPUNIT_TEST(testLargePaste);
    CPPUNIT_TEST(testRenderingOptions);
    CPPUNIT_TEST(testPasswordProtectedDocumentWithoutPassword);
    CPPUNIT_TEST(testPasswordProtectedDocumentWithWrongPassword);
    CPPUNIT_TEST(testPasswordProtectedDocumentWithCorrectPassword);
    CPPUNIT_TEST(testPasswordProtectedDocumentWithCorrectPasswordAgain);
    CPPUNIT_TEST(testImpressPartCountChanged);
    CPPUNIT_TEST_SUITE_END();

    void testPaste();
    void testLargePaste();
    void testRenderingOptions();
    void testPasswordProtectedDocumentWithoutPassword();
    void testPasswordProtectedDocumentWithWrongPassword();
    void testPasswordProtectedDocumentWithCorrectPassword();
    void testPasswordProtectedDocumentWithCorrectPasswordAgain();
    void testImpressPartCountChanged();

    static
    void sendTextFrame(Poco::Net::WebSocket& socket, const std::string& string);

    static
    bool isDocumentLoaded(Poco::Net::WebSocket& socket);

    static
    void getResponseMessage(Poco::Net::WebSocket& socket,
                            const std::string& prefix,
                            std::string& response,
                            const bool isLine);
public:
    HTTPWSTest()
        : _uri("http://127.0.0.1:" + std::to_string(ClientPortNumber)),
          _session(_uri.getHost(), _uri.getPort()),
          _request(Poco::Net::HTTPRequest::HTTP_GET, "/ws")
    {
    }

    void setUp()
    {
    }

    void tearDown()
    {
    }
};

void HTTPWSTest::testPaste()
{
    try
    {
        Poco::Net::WebSocket socket(_session, _request, _response);

        // Load a document and make it empty.
        const std::string documentPath = TDOC "/hello.odt";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();

        sendTextFrame(socket, "load url=" + documentURL);
        sendTextFrame(socket, "status");
        CPPUNIT_ASSERT_MESSAGE("cannot load the document " + documentURL, isDocumentLoaded(socket));

        sendTextFrame(socket, "uno .uno:SelectAll");
        sendTextFrame(socket, "uno .uno:Delete");

        // Paste some text into it.
        sendTextFrame(socket, "paste mimetype=text/plain;charset=utf-8\naaa bbb ccc");

        // Check if the document contains the pasted text.
        sendTextFrame(socket, "uno .uno:SelectAll");
        sendTextFrame(socket, "gettextselection mimetype=text/plain;charset=utf-8");
        std::string selection;
        int flags;
        int n;
        do
        {
            char buffer[READ_BUFFER_SIZE];
            n = socket.receiveFrame(buffer, sizeof(buffer), flags);
            std::cout << "Got " << n << " bytes, flags: " << std::hex << flags << std::dec << '\n';
            if (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE)
            {
                std::cout << "Received message: " << LOOLProtocol::getAbbreviatedMessage(buffer, n) << '\n';
                const std::string line = LOOLProtocol::getFirstLine(buffer, n);
                const std::string prefix = "textselectioncontent: ";
                if (line.find(prefix) == 0)
                {
                    selection = line.substr(prefix.length());
                    break;
                }
            }
        }
        while (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);
        socket.shutdown();
        CPPUNIT_ASSERT_EQUAL(std::string("aaa bbb ccc"), selection);
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testLargePaste()
{
    try
    {
        Poco::Net::WebSocket socket(_session, _request, _response);

        // Load a document and make it empty.
        std::string documentPath = TDOC "/hello.odt";
        std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();

        sendTextFrame(socket, "load url=" + documentURL);
        sendTextFrame(socket, "status");
        CPPUNIT_ASSERT_MESSAGE("cannot load the document " + documentURL, isDocumentLoaded(socket));

        sendTextFrame(socket, "uno .uno:SelectAll");
        sendTextFrame(socket, "uno .uno:Delete");

        // Paste some text into it.
        std::ifstream documentStream(documentPath);
        std::string documentContents((std::istreambuf_iterator<char>(documentStream)), std::istreambuf_iterator<char>());
        sendTextFrame(socket, "paste mimetype=text/html\n" + documentContents);

        // Check if the server is still alive.
        // This resulted first in a hang, as respose for the message never arrived, then a bit later in a Poco::TimeoutException.
        sendTextFrame(socket, "gettextselection mimetype=text/plain;charset=utf-8");
        std::string selection;
        int flags;
        int n;
        do
        {
            char buffer[READ_BUFFER_SIZE];
            n = socket.receiveFrame(buffer, sizeof(buffer), flags);
            if (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE)
            {
                std::cout << "Received message length " << n << ": " << LOOLProtocol::getAbbreviatedMessage(buffer, n) << '\n';
                std::string line = LOOLProtocol::getFirstLine(buffer, n);
                std::string prefix = "textselectioncontent: ";
                if (line.find(prefix) == 0)
                    break;
            }
        }
        while (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);
        socket.shutdown();
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testRenderingOptions()
{
    try
    {
        Poco::Net::WebSocket socket(_session, _request, _response);

        // Load a document and get its size.
        const std::string documentPath = TDOC "/hide-whitespace.odt";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();
        const std::string options = "{\"rendering\":{\".uno:HideWhitespace\":{\"type\":\"boolean\",\"value\":\"true\"}}}";
        sendTextFrame(socket, "load url=" + documentURL + " options=" + options);
        sendTextFrame(socket, "status");

        std::string status;
        int flags;
        int n;
        do
        {
            char buffer[READ_BUFFER_SIZE];
            n = socket.receiveFrame(buffer, sizeof(buffer), flags);
            std::cout << "Got " << n << " bytes, flags: " << std::hex << flags << std::dec << '\n';
            if (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE)
            {
                std::cout << "Received message: " << LOOLProtocol::getAbbreviatedMessage(buffer, n) << '\n';
                std::string line = LOOLProtocol::getFirstLine(buffer, n);
                std::string prefix = "status: ";
                if (line.find(prefix) == 0)
                {
                    status = line.substr(prefix.length());
                    break;
                }
            }
        }
        while (n > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);
        socket.shutdown();
        // Expected format is something like 'type=text parts=2 current=0 width=12808 height=1142'.
        Poco::StringTokenizer tokens(status, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(5), tokens.count());

        const std::string token = tokens[4];
        const std::string prefix = "height=";
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), token.find(prefix));
        const int height = std::stoi(token.substr(prefix.size()));
        // HideWhitespace was ignored, this was 32532, should be around 16706.
        CPPUNIT_ASSERT(height < 20000);
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testPasswordProtectedDocumentWithoutPassword()
{
    try
    {

        Poco::Net::WebSocket socket(_session, _request, _response);

        const std::string documentPath = TDOC "/password-protected.ods";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();
        // Send a load request without password first
        sendTextFrame(socket, "load url=" + documentURL);
        std::string response;

        getResponseMessage(socket, "error:", response, true);
        CPPUNIT_ASSERT_MESSAGE("failed command load: ", !response.empty());
        {
            Poco::StringTokenizer tokens(response, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);
            CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), tokens.count());

            std::string errorCommand;
            std::string errorKind;
            LOOLProtocol::getTokenString(tokens[0], "cmd", errorCommand);
            LOOLProtocol::getTokenString(tokens[1], "kind", errorKind);
            CPPUNIT_ASSERT_EQUAL(std::string("load"), errorCommand);
            CPPUNIT_ASSERT_EQUAL(std::string("passwordrequired:to-view"), errorKind);
        }
        socket.shutdown();
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testPasswordProtectedDocumentWithWrongPassword()
{
    try
    {

        Poco::Net::WebSocket socket(_session, _request, _response);

        const std::string documentPath = TDOC "/password-protected.ods";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();
        std::string response;

        // Send a load request with incorrect password
        sendTextFrame(socket, "load url=" + documentURL + " password=2");

        getResponseMessage(socket, "error:", response, true);
        CPPUNIT_ASSERT_MESSAGE("failed command load: ", !response.empty());
        {
            Poco::StringTokenizer tokens(response, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);
            CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), tokens.count());

            std::string errorCommand;
            std::string errorKind;
            LOOLProtocol::getTokenString(tokens[0], "cmd", errorCommand);
            LOOLProtocol::getTokenString(tokens[1], "kind", errorKind);
            CPPUNIT_ASSERT_EQUAL(std::string("load"), errorCommand);
            CPPUNIT_ASSERT_EQUAL(std::string("wrongpassword"), errorKind);
        }
        socket.shutdown();
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testPasswordProtectedDocumentWithCorrectPassword()
{
    try
    {

        Poco::Net::WebSocket socket(_session, _request, _response);

        const std::string documentPath = TDOC "/password-protected.ods";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();
        std::string response;

        // Send a load request with correct password
        sendTextFrame(socket, "load url=" + documentURL + " password=1");

        CPPUNIT_ASSERT_MESSAGE("cannot load the document with correct password " + documentURL, isDocumentLoaded(socket));
        socket.shutdown();
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::testPasswordProtectedDocumentWithCorrectPasswordAgain()
{
    testPasswordProtectedDocumentWithCorrectPassword();
}

void HTTPWSTest::testImpressPartCountChanged()
{
    try
    {
        Poco::Net::WebSocket socket(_session, _request, _response);

        // Load a document
        const std::string documentPath = TDOC "/insert-delete.odp";
        const std::string documentURL = "file://" + Poco::Path(documentPath).makeAbsolute().toString();

        sendTextFrame(socket, "load url=" + documentURL);
        sendTextFrame(socket, "status");
        CPPUNIT_ASSERT_MESSAGE("cannot load the document " + documentURL, isDocumentLoaded(socket));

        std::string response;

        // check total slides 1
        sendTextFrame(socket, "status");
        getResponseMessage(socket, "status:", response, true);
        CPPUNIT_ASSERT_MESSAGE("failed command status: ", !response.empty());
        {
            Poco::StringTokenizer tokens(response, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);
            CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(5), tokens.count());

            // Expected format is something like 'type= parts= current= width= height='.
            const std::string prefix = "parts=";
            const int totalParts = std::stoi(tokens[1].substr(prefix.size()));
            CPPUNIT_ASSERT_EQUAL(totalParts, 1);
        }

        /* FIXME partscountchanged: was removed, update accordingly
        // insert 10 slides
        for (unsigned it = 1; it <= 10; it++)
        {
            sendTextFrame(socket, "uno .uno:InsertPage");
            getResponseMessage(socket, "partscountchanged:", response, false);
            CPPUNIT_ASSERT_MESSAGE("failed command partscountchanged: ", !response.empty());
            {
                Poco::JSON::Parser parser;
                Poco::Dynamic::Var result = parser.parse(response);
                Poco::DynamicStruct values = *result.extract<Poco::JSON::Object::Ptr>();
                CPPUNIT_ASSERT(values["action"] == "PartInserted");
            }
        }

        // delete 10 slides
        for (unsigned it = 1; it <= 10; it++)
        {
            sendTextFrame(socket, "uno .uno:DeletePage");
            getResponseMessage(socket, "partscountchanged:", response, false);
            CPPUNIT_ASSERT_MESSAGE("failed command partscountchanged: ", !response.empty());
            {
                Poco::JSON::Parser parser;
                Poco::Dynamic::Var result = parser.parse(response);
                Poco::DynamicStruct values = *result.extract<Poco::JSON::Object::Ptr>();
                CPPUNIT_ASSERT(values["action"] == "PartDeleted");
            }
        }

        // undo delete slides
        for (unsigned it = 1; it <= 10; it++)
        {
            sendTextFrame(socket, "uno .uno:Undo");
            getResponseMessage(socket, "partscountchanged:", response, false);
            CPPUNIT_ASSERT_MESSAGE("failed command partscountchanged: ", !response.empty());
            {
                Poco::JSON::Parser parser;
                Poco::Dynamic::Var result = parser.parse(response);
                Poco::DynamicStruct values = *result.extract<Poco::JSON::Object::Ptr>();
                CPPUNIT_ASSERT(values["action"] == "PartInserted");
            }
        }

        // redo inserted slides
        for (unsigned it = 1; it <= 10; it++)
        {
            sendTextFrame(socket, "uno .uno:Redo");
            getResponseMessage(socket, "partscountchanged:", response, false);
            CPPUNIT_ASSERT_MESSAGE("failed command partscountchanged: ", !response.empty());
            {
                Poco::JSON::Parser parser;
                Poco::Dynamic::Var result = parser.parse(response);
                Poco::DynamicStruct values = *result.extract<Poco::JSON::Object::Ptr>();
                CPPUNIT_ASSERT(values["action"] == "PartDeleted");
            }
        }
        */

        socket.shutdown();
    }
    catch (const Poco::Exception& exc)
    {
        CPPUNIT_ASSERT_MESSAGE(exc.displayText(), false);
    }
}

void HTTPWSTest::sendTextFrame(Poco::Net::WebSocket& socket, const std::string& string)
{
    socket.sendFrame(string.data(), string.size());
}

bool HTTPWSTest::isDocumentLoaded(Poco::Net::WebSocket& ws)
{
    bool isLoaded = false;
    try
    {
        int flags;
        int bytes;
        int retries = 30;
        const Poco::Timespan waitTime(1000000);

        ws.setReceiveTimeout(0);
        std::cout << "==> isDocumentLoaded\n";
        do
        {
            char buffer[READ_BUFFER_SIZE];

            if (ws.poll(waitTime, Poco::Net::Socket::SELECT_READ))
            {
                bytes = ws.receiveFrame(buffer, sizeof(buffer), flags);
                std::cout << "Got " << bytes << " bytes, flags: " << std::hex << flags << std::dec << '\n';
                if (bytes > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    std::cout << "Received message: " << LOOLProtocol::getAbbreviatedMessage(buffer, bytes) << '\n';
                    const std::string line = LOOLProtocol::getFirstLine(buffer, bytes);
                    const std::string prefixIndicator = "statusindicatorfinish:";
                    const std::string prefixStatus = "status:";
                    if (line.find(prefixIndicator) == 0 || line.find(prefixStatus) == 0)
                    {
                        isLoaded = true;
                        break;
                    }
                }
                retries = 10;
            }
            else
            {
                std::cout << "Timeout\n";
                --retries;
            }
        }
        while (retries > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);
    }
    catch (const Poco::Net::WebSocketException& exc)
    {
        std::cout << exc.message();
    }

    return isLoaded;
}

void HTTPWSTest::getResponseMessage(Poco::Net::WebSocket& ws, const std::string& prefix, std::string& response, const bool isLine)
{
    try
    {
        int flags;
        int bytes;
        int retries = 20;
        const Poco::Timespan waitTime(1000000);

        response.clear();
        ws.setReceiveTimeout(0);
        std::cout << "==> getResponseMessage(" << prefix << ")\n";
        do
        {
            char buffer[READ_BUFFER_SIZE];

            if (ws.poll(waitTime, Poco::Net::Socket::SELECT_READ))
            {
                bytes = ws.receiveFrame(buffer, sizeof(buffer), flags);
                std::cout << "Got " << bytes << " bytes, flags: " << std::hex << flags << std::dec << '\n';
                if (bytes > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE)
                {
                    std::cout << "Received message: " << LOOLProtocol::getAbbreviatedMessage(buffer, bytes) << '\n';
                    const std::string message = isLine ?
                                                LOOLProtocol::getFirstLine(buffer, bytes) :
                                                std::string(buffer, bytes);

                    if (message.find(prefix) == 0)
                    {
                        response = message.substr(prefix.length());
                        break;
                    }
                }
                retries = 10;
            }
            else
            {
                std::cout << "Timeout\n";
                --retries;
            }
        }
        while (retries > 0 && (flags & Poco::Net::WebSocket::FRAME_OP_BITMASK) != Poco::Net::WebSocket::FRAME_OP_CLOSE);
    }
    catch (const Poco::Net::WebSocketException& exc)
    {
        std::cout << exc.message();
    }
}

CPPUNIT_TEST_SUITE_REGISTRATION(HTTPWSTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
