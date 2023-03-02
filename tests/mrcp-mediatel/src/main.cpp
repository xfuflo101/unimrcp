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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "mrcp_mediatel.h"


#define TEST_EX(x, text)          \
    if (!(x)) {                   \
        std::stringstream __ss;   \
        __ss << text;             \
        throw std::runtime_error(__ss.str()); } else ((void)0)


static
bool endsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

static
bool removeSuffix(std::string & str, std::string_view suffix) {
    if (!endsWith(str, suffix))
        return false;

    std::string_view tmp_sv(str);
    tmp_sv.remove_suffix(suffix.size());
    str.assign(tmp_sv);
    return true;
}


struct TestUnit {
    std::string name;
    std::string msg;
    std::string msg_hdr_space;
};


std::map<std::string, TestUnit> testUnits;


void initTestUnits(const std::string & dir_path) {

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        const std::filesystem::path & path = entry.path();
        if (!std::filesystem::is_regular_file(path))
            continue;

        // std::cout << "TEST: reading file = " << path.filename() << std::endl;

        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            std::cout << "TEST: READING_ERROR, file is not open, file = " << path.filename() << std::endl;
            continue;
        }

        // Read contents
        std::string value{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

        // Close the file
        file.close();

        std::string name = path.filename();
        const bool hdr_space(removeSuffix(name, ".hdr_space"));

        TestUnit & tu(testUnits[name]);
        tu.name = name;

        if (hdr_space) {
            tu.msg_hdr_space = value;
        } else {
            tu.msg = value;
        }
    }
}


static
void runTest(std::string_view name, std::string_view msgStr, std::string_view templStr) {
    std::cout << std::endl;
    std::cout << "TEST: ===============================================" << std::endl;
    std::cout << "TEST: decoding file = " << name << std::endl;
    // std::cout << msgStr << std::endl;
    std::cout << "\nTEST: ---DECODING---" << std::endl;

    mrcp::MrcpMessage msg;
    const bool decode_res = mrcp::decode(msgStr.data(), msgStr.size(), msg);
    TEST_EX(decode_res, "decoding FAILED, name = " << name);

    std::cout << "TEST: mrcp::decode res = " << std::boolalpha << decode_res << std::endl;
    // std::cout << "TEST: MrcpMessage =\n" << mrcp::MrcpMessageManip(msg) << std::endl;

    // encoding
    std::cout << "\nTEST: ---ENCODING---" << std::endl;

    std::string resultStr;
    const bool encode_res = mrcp::encode(msg, resultStr);
    TEST_EX(encode_res, "encoding FAILED, name = " << name);

    std::cout << "TEST: mrcp::encode res = " << std::boolalpha << encode_res << std::endl;
    if (encode_res) {
        // std::cout << "TEST: resultStr\n" << resultStr << std::endl;
    }

    //
    const bool decode_encode_res = (templStr == resultStr);
    TEST_EX(decode_encode_res, "decode_encode FAILED, name = " << name);

    std::cout << "\nTEST: ---DECODING_ENCODING---" << std::endl;
    std::cout << "TEST: decode_encode_res = " << std::boolalpha << decode_encode_res << "; templ_len = " << templStr.size() << "; result_len = " << resultStr.size() << std::endl;
}


static
void runTest(const TestUnit & tu) {
    if (tu.msg_hdr_space.empty())
        runTest(tu.name, tu.msg, tu.msg);
    else {
        runTest(tu.name, tu.msg, tu.msg_hdr_space);
        runTest(tu.name + ".hdr_space", tu.msg_hdr_space, tu.msg_hdr_space);
    }
}


int main(int argc, const char * const *argv)
{
    if(!mrcp::initialize()) {
      /* one time mrcp global initialization */
        std::cout << "TEST: mrcp::initialize() FAILED" << std::endl;
        return 0;
    }

    initTestUnits("v2");
    TEST_EX(!testUnits.empty(), "testUnits is emprty");

    for (const auto & elem : testUnits) {
        runTest(elem.second);
    }

    // runTest(testUnits["speak_resp.msg"]);

    /* final mrcp global termination */
    mrcp::terminate();

    std::cout << "TEST: SUCCESS" << std::endl;

    return 0;
}
