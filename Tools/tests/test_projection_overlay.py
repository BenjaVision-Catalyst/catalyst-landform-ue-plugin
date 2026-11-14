#!/usr/bin/env python3
"""Check overlay isolation/refusal/idempotence using temporary licensed engine-source fixtures."""
import importlib.util,json,shutil,tempfile,unittest,os
from pathlib import Path
script=Path(__file__).resolve().parents[1]/'stage-nanite-projection.py'
spec=importlib.util.spec_from_file_location('overlay',script);overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)
engine_source=Path(os.environ.get('UE_ENGINE_ROOT','/path/to/UnrealEngine'))/'Engine/Shaders/Private/Nanite'
@unittest.skipUnless(engine_source.is_dir(),"Set UE_ENGINE_ROOT to test the licensed local shader source")
class OverlayTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  self.project=self.root/'Project';self.engine=self.root/'EngineRoot';self.source=self.engine/'Engine/Shaders/Private/Nanite';self.source.mkdir(parents=True)
  version=self.engine/'Engine/Build/Build.version';version.parent.mkdir(parents=True);version.write_text(json.dumps(dict(MajorVersion=5,MinorVersion=8,PatchVersion=4)))
  for name in overlay.QUALIFIED:shutil.copyfile(engine_source/name,self.source/name)
  (self.source/'Other.ush').write_text('// untouched engine fixture\n')
 def test_isolated_idempotent(self):
  before={p.name:p.read_bytes() for p in self.source.iterdir()}
  a=overlay.stage(self.project,self.engine);b=overlay.stage(self.project,self.engine)
  self.assertEqual(a,b);self.assertEqual(before,{p.name:p.read_bytes() for p in self.source.iterdir()})
  target=self.project/'Saved/NaniteProjection/EngineShaders/Nanite'
  self.assertIn('CLNaniteProjectNode',(target/overlay.CLUSTER).read_text());self.assertEqual((target/'Other.ush').read_bytes(),before['Other.ush'])
 def test_tessellation_patch_projection_precedes_bounds(self):
  overlay.stage(self.project,self.engine)
  text=(self.project/'Saved/NaniteProjection/EngineShaders/Nanite'/overlay.SPLIT).read_text()
  projection=text.index('Verts[CLVertex].Position=CLNaniteProjectLocalPosition')
  self.assertLess(projection,text.index('BoundsMin = min( BoundsMin, PointPostDeform )'))
  self.assertLess(projection,text.index('PointView[i] = mul'))
  self.assertIn('CLNaniteProjection.ush',text)
 def test_voxel_projection_covers_raster_material_and_velocity(self):
  overlay.stage(self.project,self.engine)
  target=self.project/'Saved/NaniteProjection/EngineShaders/Nanite'
  raster=(target/overlay.RASTER).read_text();deform=(target/overlay.DEFORM).read_text();factory=(target/overlay.FACTORY).read_text();cluster=(target/overlay.CLUSTER).read_text()
  self.assertLess(raster.index('SkinningTransform4x3 = CLVoxelProjectTransform'),raster.index('float3x3 SkinningTransform3x3'))
  self.assertIn('SampleVoxelPerBrickSkinningTransform( InstanceData, SkinningHeader, Brick )',raster)
  self.assertIn('CLVoxelReconstruct(PrimitiveData,InstanceData',deform)
  self.assertIn('return CLVoxelVelocity',deform)
  self.assertIn('if(Cluster.bVoxel&&CLVoxelProjected(PrimitiveData)) bEvaluateWPO=true;',factory)
  self.assertGreater(cluster.index('CLNaniteProjectNode(PrimitiveData'),cluster.index('SkinningTransform'))
 def test_unknown_source_refused(self):
  # Every qualified hook, including early instance hierarchy admission, must
  # refuse an unknown vendor source before creating any generated files.
  for name in overlay.QUALIFIED:
   with self.subTest(shader=name):
    source=self.source/name;original=source.read_bytes()
    source.write_text('// changed source\n')
    with self.assertRaises(ValueError):overlay.stage(self.project,self.engine)
    self.assertFalse((self.project/'Saved').exists())
    source.write_bytes(original)
 def test_unrelated_overlay_edit_preserved(self):
  overlay.stage(self.project,self.engine);p=self.project/'Saved/NaniteProjection/EngineShaders/Nanite'/overlay.CLUSTER
  p.write_text('user edit\n')
  with self.assertRaises(ValueError):overlay.stage(self.project,self.engine)
  self.assertEqual(p.read_text(),'user edit\n')
 def test_unknown_version_refused(self):
  (self.engine/'Engine/Build/Build.version').write_text(json.dumps(dict(MajorVersion=5,MinorVersion=8,PatchVersion=0)))
  with self.assertRaises(ValueError):overlay.stage(self.project,self.engine)
  self.assertFalse((self.project/'Saved').exists())
if __name__=='__main__':unittest.main()
