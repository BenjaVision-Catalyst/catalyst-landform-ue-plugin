"""Validate local VPP adapter source qualification and generated-file ownership."""
import importlib.util,os,shutil,tempfile,unittest,json
from pathlib import Path
script=Path(__file__).resolve().parents[1]/'stage-vpp-material-bridge.py'
spec=importlib.util.spec_from_file_location('bridge',script);bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
source=Path(os.environ.get('VPP_PLUGIN_ROOT','/path/to/Voxel'))
@unittest.skipUnless((source/bridge.IMPL).is_file(),'Set VPP_PLUGIN_ROOT to the licensed local plugin')
class MaterialBridgeTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.plugin=self.root/'Landform';self.voxel=self.root/'Voxel'
  for rel in bridge.QUALIFIED:
   p=self.voxel/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/rel,p)
 def test_idempotent_without_vendor_mutations(self):
  before={r:(self.voxel/r).read_bytes() for r in bridge.QUALIFIED};bridge.stage(self.plugin,self.voxel)
  target=self.plugin/'Intermediate/VPPMaterialBridge';a=(target/'CLVPPSelection.inc').read_bytes();m=(target/'manifest.json').read_bytes();bridge.stage(self.plugin,self.voxel)
  self.assertEqual(a,(target/'CLVPPSelection.inc').read_bytes());self.assertEqual(m,(target/'manifest.json').read_bytes());self.assertEqual(before,{r:(self.voxel/r).read_bytes() for r in bridge.QUALIFIED})
 def test_each_unknown_contract_refused_before_output(self):
  for rel in bridge.QUALIFIED:
   with self.subTest(contract=rel):
    p=self.voxel/rel;before=p.read_bytes();p.write_text('// changed input\n')
    with self.assertRaises(ValueError):bridge.stage(self.plugin,self.voxel)
    self.assertFalse(self.plugin.exists());p.write_bytes(before)
 def test_unrelated_generated_edit_preserved(self):
  bridge.stage(self.plugin,self.voxel);p=self.plugin/'Intermediate/VPPMaterialBridge/CLVPPSelection.inc';p.write_text('// user edit\n')
  with self.assertRaises(ValueError):bridge.stage(self.plugin,self.voxel)
  self.assertEqual(p.read_text(),'// user edit\n')
if __name__=='__main__':unittest.main()
