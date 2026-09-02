// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    stateindex::StateId s(42); stateindex::StateGeneration g(3);
    std::cout << "StateId=" << s << " StateGeneration=" << g << " null=" << stateindex::StateId().is_null() << "\n";
    return 0;
}
