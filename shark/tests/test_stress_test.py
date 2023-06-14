# Copyright 2022 The Nod Team. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import pytest
import subprocess
import sys
import importlib.util


def test_stress_test():
    subprocess.check_call(
        [
            sys.executable,
            importlib.util.find_spec("shark.stress_test").origin,
            "--model=squeezenet1_0",
            "--devices",
            "cpu",
            "--max-iterations=1",
        ]
    )
