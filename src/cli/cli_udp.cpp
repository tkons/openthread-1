/*
 *  Copyright (c) 2017, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements a simple CLI for the CoAP service.
 */

#include "cli_udp.hpp"

#include <openthread/message.h>
#include <openthread/udp.h>

#include "cli/cli.hpp"
#include "cli/cli_server.hpp"
#include "common/encoding.hpp"

using ot::Encoding::BigEndian::HostSwap16;

namespace ot {
namespace Cli {

const struct UdpExample::Command UdpExample::sCommands[] = {
    {"help", &UdpExample::ProcessHelp},       {"bind", &UdpExample::ProcessBind}, {"close", &UdpExample::ProcessClose},
    {"connect", &UdpExample::ProcessConnect}, {"open", &UdpExample::ProcessOpen}, {"send", &UdpExample::ProcessSend}};

UdpExample::UdpExample(Interpreter &aInterpreter)
    : mInterpreter(aInterpreter)
{
    memset(&mSocket, 0, sizeof(mSocket));
}

otError UdpExample::ProcessHelp(int argc, char *argv[])
{
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);

    for (unsigned int i = 0; i < OT_ARRAY_LENGTH(sCommands); i++)
    {
        mInterpreter.mServer->OutputFormat("%s\r\n", sCommands[i].mName);
    }

    return OT_ERROR_NONE;
}

otError UdpExample::ProcessBind(int argc, char *argv[])
{
    otError    error;
    otSockAddr sockaddr;
    long       value;

    VerifyOrExit(argc == 2, error = OT_ERROR_INVALID_ARGS);

    memset(&sockaddr, 0, sizeof(sockaddr));

    error = otIp6AddressFromString(argv[0], &sockaddr.mAddress);
    SuccessOrExit(error);

    error = Interpreter::ParseLong(argv[1], value);
    SuccessOrExit(error);

    sockaddr.mPort = static_cast<uint16_t>(value);

    error = otUdpBind(&mSocket, &sockaddr);

exit:
    return error;
}

otError UdpExample::ProcessConnect(int argc, char *argv[])
{
    otError    error;
    otSockAddr sockaddr;
    long       value;

    VerifyOrExit(argc == 2, error = OT_ERROR_INVALID_ARGS);

    memset(&sockaddr, 0, sizeof(sockaddr));

    error = otIp6AddressFromString(argv[0], &sockaddr.mAddress);
    SuccessOrExit(error);

    error = Interpreter::ParseLong(argv[1], value);
    SuccessOrExit(error);

    sockaddr.mPort = static_cast<uint16_t>(value);

    error = otUdpConnect(&mSocket, &sockaddr);

exit:
    return error;
}

otError UdpExample::ProcessClose(int argc, char *argv[])
{
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);

    return otUdpClose(&mSocket);
}

otError UdpExample::ProcessOpen(int argc, char *argv[])
{
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);

    return otUdpOpen(mInterpreter.mInstance, &mSocket, HandleUdpReceive, this);
}

otError UdpExample::ProcessSend(int argc, char *argv[])
{
    otError       error = OT_ERROR_NONE;
    otMessageInfo messageInfo;
    otMessage *   message       = NULL;
    int           curArg        = 0;
    uint16_t      payloadLength = 0;
    PayloadType   payloadType   = kTypeText;

    memset(&messageInfo, 0, sizeof(messageInfo));

    VerifyOrExit(argc >= 1 && argc <= 4, error = OT_ERROR_INVALID_ARGS);

    if (argc > 2)
    {
        long value;
        error = otIp6AddressFromString(argv[curArg++], &messageInfo.mPeerAddr);
        SuccessOrExit(error);

        error = Interpreter::ParseLong(argv[curArg++], value);
        SuccessOrExit(error);

        messageInfo.mPeerPort = static_cast<uint16_t>(value);
    }

    if (argc == 2 || argc == 4)
    {
        int typePos = curArg++;

        if (strcmp(argv[typePos], "-s") == 0)
        {
            unsigned long value;
            payloadType = kTypeAutoSize;
            SuccessOrExit(error = Interpreter::ParseUnsignedLong(argv[curArg], value));
            payloadLength = static_cast<uint16_t>(value);
        }
        else if (strcmp(argv[typePos], "-x") == 0)
        {
            payloadLength = static_cast<uint16_t>(strlen(argv[curArg]));
            payloadType   = kTypeHexString;
        }
        else if (strcmp(argv[typePos], "-t") == 0)
        {
            payloadType = kTypeText;
        }
    }

    message = otUdpNewMessage(mInterpreter.mInstance, NULL);
    VerifyOrExit(message != NULL, error = OT_ERROR_NO_BUFS);

    switch (payloadType)
    {
    case kTypeText:
        SuccessOrExit(error = otMessageAppend(message, argv[curArg], static_cast<uint16_t>(strlen(argv[curArg]))));
        break;
    case kTypeAutoSize:
        SuccessOrExit(error = WriteCharToBuffer(message, payloadLength));
        break;
    case kTypeHexString:
    {
        uint8_t     buf[50];
        int16_t     bufLen;
        uint16_t    conversionLength = 0;
        const char *hexString        = argv[curArg];

        while (payloadLength > 0)
        {
            bufLen = static_cast<int16_t>(Interpreter::Hex2Bin(hexString, buf, sizeof(buf), true));

            VerifyOrExit(bufLen > 0, error = OT_ERROR_INVALID_ARGS);

            conversionLength = static_cast<uint16_t>(bufLen * 2);

            if ((payloadLength & 0x01) != 0)
            {
                conversionLength -= 1;
            }

            hexString += conversionLength;
            payloadLength -= conversionLength;
            SuccessOrExit(error = otMessageAppend(message, buf, static_cast<uint16_t>(bufLen)));
        }
        break;
    }
    }

    error = otUdpSend(&mSocket, message, &messageInfo);

exit:

    if (error != OT_ERROR_NONE && message != NULL)
    {
        otMessageFree(message);
    }

    return error;
}

otError UdpExample::WriteCharToBuffer(otMessage *aMessage, uint16_t aMessageSize)
{
    otError error     = OT_ERROR_NONE;
    uint8_t character = 0x30; // 0

    for (uint16_t index = 0; index < aMessageSize; index++)
    {
        SuccessOrExit(error = otMessageAppend(aMessage, &character, 1));
        character++;

        switch (character)
        {
        case 0x3A:            // 9
            character = 0x41; // A
            break;
        case 0x5B:            // Z
            character = 0x61; // a
            break;
        case 0x7B:            // z
            character = 0x30; // 0
            break;
        }
    }

exit:
    return error;
}

otError UdpExample::Process(int argc, char *argv[])
{
    otError error = OT_ERROR_PARSE;

    if (argc < 1)
    {
        ProcessHelp(0, NULL);
        error = OT_ERROR_INVALID_ARGS;
    }
    else
    {
        for (size_t i = 0; i < OT_ARRAY_LENGTH(sCommands); i++)
        {
            if (strcmp(argv[0], sCommands[i].mName) == 0)
            {
                error = (this->*sCommands[i].mCommand)(argc - 1, argv + 1);
                break;
            }
        }
    }
    return error;
}

void UdpExample::HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    static_cast<UdpExample *>(aContext)->HandleUdpReceive(aMessage, aMessageInfo);
}

void UdpExample::HandleUdpReceive(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t buf[1500];
    int     length;

    mInterpreter.mServer->OutputFormat("%d bytes from ", otMessageGetLength(aMessage) - otMessageGetOffset(aMessage));
    mInterpreter.OutputIp6Address(aMessageInfo->mPeerAddr);
    mInterpreter.mServer->OutputFormat(" %d ", aMessageInfo->mPeerPort);

    length      = otMessageRead(aMessage, otMessageGetOffset(aMessage), buf, sizeof(buf) - 1);
    buf[length] = '\0';

    mInterpreter.mServer->OutputFormat("%s\r\n", buf);
}

} // namespace Cli
} // namespace ot
