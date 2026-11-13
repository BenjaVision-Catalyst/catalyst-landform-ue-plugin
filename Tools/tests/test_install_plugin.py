import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('installer', Path(__file__).parents[1]/'install-plugin.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root/'source'
        self.source.mkdir()
        (self.source/'CatalystLandform.uplugin').write_text('{}')
        guides = self.source/'Content/RadialGuides/DA_ExampleRadialGuides.uasset'
        guides.parent.mkdir(parents=True)
        guides.write_bytes(b'packaged-guides')
        self.project = self.root/'project/PlanetGame.uproject'
        self.project.parent.mkdir()
        self.project.write_text('{"FileVersion":3,"Plugins":[{"Name":"OtherPlugin","Enabled":true}]}')
        self.dependencies = {}
        for name in ('CatalystFoundation', 'Voxel'):
            source = self.root/name
            source.mkdir()
            (source/(name+'.uplugin')).write_text(json.dumps({'Modules':[
                {'Name':'CatalystFoundation'}, {'Name':'CatalystFoundationEditor'}, {'Name':'OtherIntegration'}]}))
            for module in ('CatalystFoundation', 'CatalystFoundationEditor'):
                folder = source/'Source'/module
                folder.mkdir(parents=True)
                (folder/'module.cpp').write_text('// source')
            self.dependencies[name] = source

    def install(self):
        with patch.object(installer.subprocess, 'check_output', return_value=''):
            return installer.install(self.source, self.project, self.dependencies)

    def test_reinstall_preserves_project_and_dependency_ownership(self):
        self.install()
        self.install()  # Full Foundation source matches its installed module subset.
        entries = json.loads(self.project.read_text())['Plugins']
        self.assertEqual(1, sum(p['Name']=='OtherPlugin' for p in entries))
        self.assertTrue(all(next(p for p in entries if p['Name']==n)['Enabled'] for n in self.dependencies))
        self.assertEqual(b'packaged-guides', (self.project.parent/'Plugins/CatalystLandform/Content/RadialGuides/DA_ExampleRadialGuides.uasset').read_bytes())
        self.assertFalse((self.project.parent/'Plugins/CatalystConnect').exists())

    def test_refuses_to_replace_local_plugin_edits(self):
        self.install()
        local = self.project.parent/'Plugins/CatalystLandform/local.cpp'
        local.write_text('author edit')
        with self.assertRaisesRegex(SystemExit, 'unmanaged or edited'):
            self.install()
        self.assertEqual('author edit', local.read_text())

    def test_does_not_copy_build_products(self):
        for name in ('Binaries', 'Intermediate', 'Saved'):
            (self.source/name).mkdir()
            (self.source/name/'generated').write_text('generated')
        self.install()
        self.assertFalse((self.project.parent/'Plugins/CatalystLandform/Binaries').exists())
        self.assertFalse((self.project.parent/'Plugins/CatalystLandform/Intermediate').exists())
        self.assertFalse((self.project.parent/'Plugins/CatalystLandform/Saved').exists())


if __name__ == '__main__':
    unittest.main()
