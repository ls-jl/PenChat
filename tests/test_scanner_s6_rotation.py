#!/usr/bin/env python3
"""S6 clockwise90 relative to its original CCW90 view means net unrotated."""
import pathlib
import struct
import tempfile
from test_scanner_process import Scanner

for profile in ('s6', 'orange', 'y02', 'melon'):
    with tempfile.TemporaryDirectory(prefix='penchat-scan-rotation-') as directory:
        worker = Scanner(directory, profile=profile)
        try:
            started = worker.press()
            expected = 'none' if profile == 's6' else 'counterclockwise90'
            assert started['sensor_rotation'] == expected, started
            worker.frames(40 if profile == 's6' else 1)
            worker.release()
            result = worker.receive('finished')
            assert result['usable'] and result['sensor_rotation'] == expected, result
            if profile == 's6':
                assert result['counts']['batches'] == 2, result['counts']
            worker.send('confirm')
            image = worker.receive('exported')['image']
            assert image['metadata']['sensor_rotation'] == expected, image
            if profile == 's6':
                assert image['metadata']['calibration'] == 's6-pag7920-optical-region-neutral-v4'
                assert image['metadata']['sensor_region'] == [112,8,160,192]
                assert (image['metadata']['sensor_width'],image['metadata']['sensor_height']) == (320,240)
                header = pathlib.Path(image['path']).read_bytes()[:24]
                assert struct.unpack('>II', header[16:24])[1] == 192
            worker.send('discard')
            worker.receive('ready')
        finally:
            worker.close()
print('SCANNER_S6_RELATIVE_CLOCKWISE_AND_OTHER_PROFILES_UNCHANGED_OK')
