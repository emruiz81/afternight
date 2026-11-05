# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import os
import zipfile
import argparse
import re

def get_clean_pyc_path(pyc_path, source_dir):
    """
    Create a cleaned-up archive path for .pyc files from __pycache__.
    Example:
        source_dir/pkg/__pycache__/mod.cpython-39.pyc → pkg/mod.pyc
    """
    rel_path = os.path.relpath(pyc_path, source_dir)
    parts = rel_path.split(os.sep)

    if len(parts) >= 2 and parts[-2] == '__pycache__':
        parent_dir = os.sep.join(parts[:-2])
        filename = parts[-1]
        match = re.match(r'(.+?)\.cpython-\d+\w*\.pyc$', filename)
        if match:
            return os.path.join(parent_dir, f"{match.group(1)}.pyc")
    return rel_path

def package_files(source_dir, zip_path, exclude_folders=None):
    """
    Package .pyc files (with cleaned names) and all other files (except .py and excluded folders) into a zip.
    """
    if exclude_folders is None:
        exclude_folders = []

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for foldername, subfolders, filenames in os.walk(source_dir):
            # Apply exclusion only at the root level
            if foldername == source_dir:
                subfolders[:] = [d for d in subfolders if d not in exclude_folders]

            for filename in filenames:
                full_path = os.path.join(foldername, filename)

                if filename.endswith('.py'):
                    continue  # Skip source files

                if filename.endswith('.pyc'):
                    arcname = get_clean_pyc_path(full_path, source_dir)
                else:
                    arcname = os.path.relpath(full_path, source_dir)

                zipf.write(full_path, arcname)
                
def remove_previous_packages(path):
    # Loop through all files in the current directory
    for filename in os.listdir(path):
        if filename.startswith("python") and filename.endswith(".zip"):
            file_path = os.path.join(path, filename)
            try:
                os.remove(file_path)
                print(f"Deleted: {file_path}")
            except Exception as e:
                print(f"Failed to delete {file_path}: {e}")
                
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Package compiled .pyc files and other resources into a zip archive.")
    parser.add_argument("source_dir", help="Path to the source directory.")
    parser.add_argument("output_zip", help="Path to the output zip file.")
    parser.add_argument("--exclude", nargs='*', default=[], 
                        help="List of folders to exclude at the root level (default: site-packages).")

    args = parser.parse_args()

    print("Removing previous packaged files...")
    remove_previous_packages(os.path.dirname(args.output_zip))
    
    print("Packaging files...")
    package_files(args.source_dir, args.output_zip, args.exclude)
    print("Done!")
