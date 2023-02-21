/*
 * Copyright 2008-2015 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>

#include "mrcp_mediatel.h"


const char * test_recognize_msg = ""
"MRCP/2.0 903 RECOGNIZE 543257\r\n"
"Channel-Identifier:32AECB23433801@speechrecog\r\n"
"Confidence-Threshold:0.9\r\n"
"Content-Type:application/srgs+xml\r\n"
"Content-ID:<request1@form-level.store>\r\n"
"Content-Length:702\r\n"
"\r\n"
"<?xml version=\"1.0\"?>\r\n"
"\r\n"
"<!-- the default grammar language is US English -->\r\n"
"<grammar xmlns=\"http://www.w3.org/2001/06/grammar\"\r\n"
"        xml:lang=\"en-US\" version=\"1.0\" root=\"request\">\r\n"
"\r\n"
"<!-- single language attachment to tokens -->\r\n"
"     <rule id=\"yes\">\r\n"
"           <one-of>\r\n"
"                 <item xml:lang=\"fr-CA\">oui</item>\r\n"
"                 <item xml:lang=\"en-US\">yes</item>\r\n"
"           </one-of>\r\n"
"     </rule>\r\n"
"\r\n"
"<!-- single language attachment to a rule expansion -->\r\n"
"     <rule id=\"request\">\r\n"
"           may I speak to\r\n"
"           <one-of xml:lang=\"fr-CA\">\r\n"
"                 <item>Michel Tremblay</item>\r\n"
"                 <item>Andre Roy</item>\r\n"
"           </one-of>\r\n"
"     </rule>\r\n"
"</grammar>"
;


int main(int argc, const char * const *argv)
{
    /* one time mrcp global initialization */
    if(!mrcp::initialize()) {
        std::cout << "TEST: mrcp::initialize() FAILED" << std::endl;
        return 0;
    }


    mrcp::MrcpMessage msg;
    bool res = mrcp::decode(test_recognize_msg, msg);


    /* final mrcp global termination */
    mrcp::terminate();

    return 0;
}
