"""Imports SourceAudio/*.wav into /Game/Audio as SoundWave assets.

Runs inside the editor's Python commandlet; use Scripts/ImportSounds.bat.
Re-running replaces existing assets, so edit or replace a WAV and re-run.
"""

import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(ROOT, "SourceAudio")
DEST = "/Game/Audio"

tasks = []
for file_name in sorted(os.listdir(SOURCE_DIR)):
    if not file_name.lower().endswith(".wav"):
        continue
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE_DIR, file_name)
    task.destination_path = DEST
    task.destination_name = os.path.splitext(file_name)[0]
    task.replace_existing = True
    task.automated = True
    task.save = True
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for task in tasks:
    paths = list(task.imported_object_paths)
    unreal.log("ArenaImport: {} -> {}".format(os.path.basename(task.filename), paths[0] if paths else "FAILED"))
