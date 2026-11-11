#!/usr/bin/env python3
"""Serial, native UE/VPP comparison. Requires the LandformLab prototype host built.

Does not build, copy source, rewrite assets, change VPP settings persistently or
change the designer UI. Use --create-graph once in a disposable editor project.
"""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def run(cmd, folder, timeout=1200):
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "command.json").write_text(json.dumps(cmd, indent=2) + "\n")
    start = time.monotonic()
    with (folder / "stdout.log").open("w") as out:
        result = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, timeout=timeout)
    print(folder.name, "exit", result.returncode, "wall seconds", round(time.monotonic()-start, 3), flush=True)
    if result.returncode:
        raise RuntimeError(f"Native process failed; inspect {folder}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="UE 5.7 engine checkout root")
    parser.add_argument("--project", type=Path, required=True, help="Physical isolated LandformLab.uproject")
    parser.add_argument("--output", type=Path, required=True, help="New comparison directory")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--create-graph", action="store_true", help="Create new native graph package; refuses to overwrite")
    parser.add_argument("--skip-saves", action="store_true", help="Performance-only rerun; omits edit persistence qualification")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("Use at least one round")
    host = args.project.resolve().parent
    plugin = host / "Plugins/CatalystLandform"
    recipe = plugin / "Examples/OrganicKarst.json"
    editor = args.engine.resolve() / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
    if not editor.is_file() or not args.project.is_file() or not recipe.is_file():
        parser.error("Missing native editor, project or canonical cave recipe")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"recipeSHA256": hashlib.sha256(recipe.read_bytes()).hexdigest(), "schedulerPolicy": "stock; no worker override",
                "resolution": [1280, 800], "voxelSizeCm": 20, "rounds": args.rounds,
                "note": "Synthetic render-only ISM workload; no ecosystem/PCG/animation budget inferred."}
    manifest["sources"] = {str(p.relative_to(host)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for root in (plugin / "Source", host / "Source/LandformLab")
                           for p in sorted(root.rglob("*")) if p.is_file()}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    common = [str(editor), str(args.project.resolve()), "/Engine/Maps/Entry", "-unattended", "-nosplash", "-nosound",
              "-notracethreading", "-nohighdpi", "-CLNaniteProjectionShaders"]
    tests = output / "parity"
    cmd = common + ["-ExecCmds=Automation RunTests Catalyst.Landform.Caves.DeliveryPrototype.Parity",
                    "-TestExit=Automation Test Queue Empty", "-ReportExportPath="+str(tests / "automation"),
                    "-abslog="+str(tests / "native.log"), "-CLDeliveryBenchmarkReport="+str(tests / "bulk.json")]
    if args.create_graph:
        cmd.append("-CLCreateDeliveryGraph")
    run(cmd, tests)
    report = read_json(tests / "automation/index.json")
    if len(report["tests"]) != 1 or report["tests"][0]["state"] != "Success":
        raise RuntimeError("Parity failed: no performance comparison accepted")
    proof_common = common.copy()
    proof_common[2] += "?game=/Script/LandformLab.CLGeneratedCaveProofGameMode"
    proof_common += ["-game", "-ResX=1280", "-ResY=800", "-windowed",
                     "-CLCaveRecipe="+str(recipe), "-ExecCmds=r.VSync 0,t.MaxFPS 0,t.IdleWhenNotForeground 0,r.ScreenPercentage,r.DynamicRes.OperationMode,sg.ResolutionQuality,sg.ShadowQuality,sg.GlobalIlluminationQuality,sg.ViewDistanceQuality"]
    backends = ["legacy", "cpp", "graph"]
    slot_prefix = "LandformDelivery_"+output.name
    results = []
    recipe_digest = None
    proxy_digest = None

    def proof(name, backend, flags):
        nonlocal recipe_digest, proxy_digest
        folder = output / name
        cmd = proof_common + ["-CLDelivery="+backend, "-CLReport="+str(folder / "result.json"),
                              "-abslog="+str(folder / "native.log")] + flags
        run(cmd, folder)
        result = read_json(folder / "result.json")
        if not result.get("passed") or not result.get("nativeOwnerStable"):
            raise RuntimeError(f"Native qualification failed: {name}")
        if result["deliveryBackend"] != backend or result["voxelSizeCm"] != 20 or result["nativeLODQuality"] != 2:
            raise RuntimeError(f"Native backend or terrain settings changed: {name}")
        if recipe_digest is None:
            recipe_digest = result["recipeSHA1"]
        if result["recipeSHA1"] != recipe_digest:
            raise RuntimeError(f"Recipe changed between trials: {name}")
        if "-CLCaveReferenceReview" in flags and (not result["referenceWalkComplete"] or not result["controlIdentityUnchanged"] or result["saveBytes"] <= 0):
            raise RuntimeError(f"Native edit/save qualification incomplete: {name}")
        if "-CLCaveEntranceProof" in flags and (result["routeDistanceMeters"] < 100 or result["routeFrames"] != result["routeGroundedFrames"]):
            raise RuntimeError(f"Entrance route/floor continuity failed: {name}")
        if "-CLCaveLoad" in flags and (result["maximumReloadErrorCm"] != 0 or result["comparedProbes"] < 100):
            raise RuntimeError(f"Native persistence changed fields: {name}")
        if "-CLHeadroom" in flags and (len(result["headroom"]) != 4 or any(x["gpuMs"]["samples"] == 0 for x in result["headroom"])):
            raise RuntimeError(f"Missing native GPU headroom evidence: {name}")
        for level in result.get("headroom", []):
            if level["peakNativePendingTasks"] or level["nativeStateBusyFrames"]:
                raise RuntimeError(f"Terrain still generating during render measurement: {name}")
            if proxy_digest is None:
                proxy_digest = level["relativeTransformsSHA1"]
            if level["relativeTransformsSHA1"] != proxy_digest:
                raise RuntimeError(f"Object fixture changed between trials: {name}")
        results.append({"name": name, "backend": backend, "result": result})
        (output / "results.json").write_text(json.dumps(results, indent=2)+"\n")

    for round_index in range(args.rounds):
        order = backends[round_index % 3:] + backends[:round_index % 3]
        for backend in order:
            proof(f"entrance-{round_index+1}-{backend}", backend, ["-CLCaveEntranceProof"])
        for backend in reversed(order):
            proof(f"headroom-{round_index+1}-{backend}", backend, ["-CLHeadroom"])
    if not args.skip_saves:
        for backend in backends:
            slot = slot_prefix+"_"+backend
            proof("writer-"+backend, backend, ["-CLCaveReferenceReview", "-CLCaveSlot="+slot])
            proof("reader-"+backend, backend, ["-CLCaveLoad", "-CLCaveWalk", "-CLCaveSlot="+slot])
        proof("reader-graph-from-cpp", "graph", ["-CLCaveLoad", "-CLCaveWalk", "-CLCaveSlot="+slot_prefix+"_cpp"])
        proof("reader-cpp-from-legacy", "cpp", ["-CLCaveLoad", "-CLCaveWalk", "-CLCaveSlot="+slot_prefix+"_legacy"])


if __name__ == "__main__":
    main()
