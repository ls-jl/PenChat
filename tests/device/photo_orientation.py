#!/usr/bin/env python3
"""ORANGE four-pose capture/crop and real MT acceptance with full data isolation."""
import pathlib,subprocess,sys
if __name__=='__main__':
    subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('display_config.py')),
                    '--serial','MEB0400008900794','--photo-gestures'],check=True)
