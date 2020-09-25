// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <ie_core.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include <ie_plugin_config.hpp>

#include "common_test_utils/common_utils.hpp"
#include "functional_test_utils/blob_utils.hpp"
#include "functional_test_utils/layer_test_utils.hpp"
#include "functional_test_utils/plugin_cache.hpp"
#include "ngraph_functions/pass/convert_prc.hpp"

#include "subgraph_tests/input_conv.hpp"

#include "ngraph_functions/builders.hpp"

namespace LayerTestsDefinitions {

std::string InputConvTest::getTestCaseName(testing::TestParamInfo<inputConvParams> obj) {
    InferenceEngine::Precision netPrecision;
    std::vector<size_t> inputShape;
    std::string targetDevice;
    std::map<std::string, std::string> configuration;
    std::tie(netPrecision, targetDevice, configuration, inputShape) = obj.param;

    std::ostringstream result;
    result << "IS=" << CommonTestUtils::vec2str(inputShape) << "_";
    result << "netPRC=" << netPrecision.name() << "_";
    result << "targetDevice=" << targetDevice;
    for (auto const& configItem : configuration) {
        result << "_configItem=" << configItem.first << "_" << configItem.second;
    }
    return result.str();
}

InferenceEngine::Blob::Ptr InputConvTest::GenerateInput(const InferenceEngine::InputInfo& info) const {
    InferenceEngine::Blob::Ptr blob = make_blob_with_precision(info.getTensorDesc());
    blob->allocate();
    auto precision = info.getPrecision();

    auto* rawBlobDataPtr = blob->buffer().as<float*>();
    auto counter = 0;
    for (size_t i = 0; i < blob->size(); i++) {
        auto value = counter;
        if (typeid(precision) == typeid(typename InferenceEngine::PrecisionTrait<InferenceEngine::Precision::FP16>::value_type)) {
            rawBlobDataPtr[i] = ngraph::float16(value).to_bits();
        } else {
            rawBlobDataPtr[i] = value;
        }

        if (counter == 15)
            counter = 0;
        else
            ++counter;
    }
    return blob;
}

void InputConvTest::SetUp() {
    auto generateWeights = [](std::size_t out_channels, std::size_t kernel_size) {
        std::vector<float> res;
        for (int i = 0; i < out_channels; ++i) {
            for (int j = 0; j < kernel_size; ++j) {
                res.emplace_back(j == 0 ? 1 : 0);
            }
        }

        return res;
    };

    InferenceEngine::Precision netPrecision;
    std::map<std::string, std::string> tempConfig;
    std::vector<size_t> inputShape;
    std::tie(netPrecision, targetDevice, tempConfig, inputShape) = this->GetParam();
    configuration.insert(tempConfig.begin(), tempConfig.end());

    auto ngPrc = FuncTestUtils::PrecisionUtils::convertIE2nGraphPrc(netPrecision);

    auto params = ngraph::builder::makeParams(ngPrc, { inputShape });

    auto conv_0 = ngraph::builder::makeConvolution(params[0], ngPrc, { 1, 9 }, { 1, 1 }, { 0, 0 },
        { 0, 0 }, { 1, 1 }, ngraph::op::PadType::VALID, 4, false, generateWeights(4, 9));

    // without reshape for int16 ConvertToFloat gives incorrect values but intermediate representation is ok
    // fp32 works with and without reshape
    std::vector<size_t> outFormShapes2 = { 1, 4 * 8 };
    auto pattern2 = std::make_shared<ngraph::opset1::Constant>(ngraph::element::Type_t::i64, ngraph::Shape{ 2 }, outFormShapes2);
    auto reshape2 = std::make_shared<ngraph::opset1::Reshape>(conv_0, pattern2, false);

    /* every second output (0, 2, 4 etc) seen with this
    auto permute_0 = std::make_shared<ngraph::op::Transpose>(conv_0,
        ngraph::op::Constant::create(ngraph::element::i64, ngraph::Shape{ 4 }, { 0, 3, 1, 2 }));
    permute_0->set_friendly_name("permute1");*/

    ngraph::ResultVector results {std::make_shared<ngraph::op::Result>(reshape2)};
    function = std::make_shared<ngraph::Function>(results, params, "InputConvTest");
}

TEST_P(InputConvTest, CompareWithRefImpl) {
    Run();
};

}  // namespace LayerTestsDefinitions