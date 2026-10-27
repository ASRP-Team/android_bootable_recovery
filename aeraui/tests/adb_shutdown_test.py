"""Execute the production StopAll implementation with mocked Android properties."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AdbShutdownTest(unittest.TestCase):
    def test_usb_survives_wireless_shutdown(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('A host C++ compiler is required')
        source = (ROOT / 'aera_adbd.cpp').read_text()
        body = source.split('bool AeraAdbd::StopAll()\n{', 1)[1].split('\n}\n', 1)[0]
        constants = '\n'.join(re.findall(r'constexpr const char\* k\w+Prop = "[^"]+";', source))
        fixture = r'''
#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>
static std::map<std::string, std::string> properties;
static std::vector<std::pair<std::string, std::string>> writes;
static unsigned pairing_stops;
static std::string GetProp(const std::string &key) { return properties[key]; }
static bool SetProp(const std::string &key, const std::string &value) {
  writes.emplace_back(key, value); properties[key] = value; return true;
}
class AeraAdbd {
 public:
  static bool StopAll();
  static bool StopPairing() { ++pairing_stops; return true; }
};
''' + constants + '\nbool AeraAdbd::StopAll()\n{' + body + r'''
}
static void Check(std::map<std::string, std::string> initial, const std::string &control) {
  properties = std::move(initial); writes.clear(); pairing_stops = 0;
  assert(AeraAdbd::StopAll() && pairing_stops == 1);
  assert(GetProp(kTlsEnableProp) == "0" && GetProp(kTcpPortProp) == "0");
  assert(GetProp(kTlsPortProp).empty() && GetProp(kTlsRequestedPortProp).empty());
  assert(GetProp(kAeraWifiOnlyProp) == "0");
  unsigned controls = 0;
  for (const auto &write : writes) {
    if (write.first.rfind("ctl.", 0) == 0) {
      assert(write.first == control && write.second == "adbd"); ++controls;
    }
    assert(write.first.rfind("sys.usb.", 0) != 0);
  }
  assert(controls == (control.empty() ? 0u : 1u));
  if (control != "ctl.stop") {
    writes.clear(); assert(AeraAdbd::StopAll());
    for (const auto &write : writes) assert(write.first.rfind("ctl.", 0) != 0);
  }
}
int main() {
  Check({}, "");
  Check({{kTcpPortProp, "0"}, {kTlsEnableProp, "0"}}, "");
  Check({{kTcpPortProp, "0"}, {kTlsEnableProp, "1"}, {kTlsPortProp, "5555"}}, "");
  Check({{kTcpPortProp, "5555"}}, "ctl.restart");
  Check({{"persist.adb.tcp.port", "5555"}}, "ctl.restart");
  Check({{kTcpPortProp, "0"}, {"persist.adb.tcp.port", "5555"}}, "");
  Check({{kTcpPortProp, "-1"}}, "");
  Check({{kAeraWifiOnlyProp, "1"}}, "ctl.restart");
  Check({{kFastbootModeProp, "1"}, {kAeraWifiOnlyProp, "1"}, {kTlsEnableProp, "1"}}, "ctl.stop");
  Check({{kFastbootModeProp, "1"}, {kTcpPortProp, "5555"}}, "ctl.stop");
}
'''
        with tempfile.TemporaryDirectory(prefix='aera-adb-stop-') as directory:
            program = pathlib.Path(directory) / 'test.cpp'
            binary = pathlib.Path(directory) / 'test'
            program.write_text(fixture)
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-UNDEBUG', str(program), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
