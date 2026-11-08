"""Scoped Y07 preview poses and one HD photo; parent owns data isolation/restoration."""
import shlex
import subprocess
import types


def jpeg_size(data):
    """Read baseline/progressive JPEG dimensions without decoding the full image."""
    assert 4 <= len(data) <= 32 * 1024 * 1024 and data[:2] == b'\xff\xd8'
    offset = 2
    while offset < len(data):
        assert data[offset] == 255, 'Malformed JPEG marker'
        while offset < len(data) and data[offset] == 255:
            offset += 1
        assert offset < len(data)
        marker = data[offset]
        offset += 1
        if marker in (0x01, 0xd8) or 0xd0 <= marker <= 0xd7:
            continue
        assert marker not in (0xd9, 0xda), 'JPEG ended before frame header'
        assert offset + 2 <= len(data)
        length = int.from_bytes(data[offset:offset + 2], 'big')
        assert length >= 2 and offset + length <= len(data)
        if marker in (0xc0, 0xc1, 0xc2):
            assert length >= 8
            height = int.from_bytes(data[offset + 3:offset + 5], 'big')
            width = int.from_bytes(data[offset + 5:offset + 7], 'big')
            assert width > 0 and height > 0
            return [width, height]
        offset += length
    raise AssertionError('JPEG frame header missing')


def run(env):
    d = types.SimpleNamespace(**env)
    assert d.SERIAL == '7G12000010905426' and d.H > d.W
    initial = d.state()
    assert initial['view'] == 'chat' and not initial['generating']
    assert not initial['keyboard'] and not initial['editor']['active']
    session, draft = initial['session'], initial['draft_length']
    image_count = len(initial['chat_images'])
    native = d.pid()
    report_active = d.shell('cat /sys/kernel/gsensor/report_active')
    pictures = '/userdisk/Pictures'
    def album_inventory():
        # Metadata only: don't hash/read the user's original photo pixels.
        return sorted(d.shell('find ' + shlex.quote(pictures) +
                              r' -maxdepth 1 -type f -exec stat -c %n:%s:%Y {} \;').splitlines())
    album_before = album_inventory()

    # Use the measured composer, not an old bottom-row coordinate. Portrait
    # tools are bottom aligned even when its text region grows upward.
    capsule = initial['composer_bounds']
    d.tap(capsule[0] + 47, initial['send_center_y2'] // 2)
    d.wait(lambda: d.state()['attachments'])
    state = d.state()
    bounds = state['attachment_actions']['camera']
    viewport = state['attachment_viewport']
    assert viewport[0] <= bounds[0] < bounds[2] <= viewport[2]
    assert viewport[1] <= bounds[1] < bounds[3] <= viewport[3]
    d.tap((bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)
    d.wait(lambda: d.state()['view'] == 'camera')
    d.wait(lambda: d.state()['camera_ready'], timeout=20)
    def camera_action(name):
        bounds=d.state()['camera_controls'][name]
        d.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)

    # The portrait workspace remains fixed. These controls are defined relative
    # to its loaded logical height in AppLayout::cameraWorkspace, not a screen
    # resolution constant. Lock auto pose before exercising deterministic turns.
    if d.state()['camera_auto']:
        camera_action('direction')
        d.wait(lambda: not d.state()['camera_auto'])
    poses = []
    for angle in (0, 90, 180, 270):
        for attempt in range(4):
            current = d.state()['camera_angle']
            if current == angle:
                break
            camera_action('turn')
            d.wait(lambda: d.state()['camera_angle'] == (current + 90) % 360)
        state = d.wait(lambda: (s if (s := d.state())['camera_angle'] == angle
                               and s['camera_ready'] else None), timeout=20)
        assert not state['camera_auto']
        expected = {'preview': 270, 'workspace': 0, 'saved': angle}
        assert state['camera_transforms'] == expected, state['camera_transforms']
        assert d.pid() == native
        d.capture('camera-orientation-preview-' + str(angle))
        poses.append({'pose': angle, 'transforms': state['camera_transforms']})

    # One true HD capture at the frozen 270-degree pose. Cancel the crop instead
    # of attaching/sending or touching the album opt-in checkbox.
    camera_action('shutter')
    state = d.wait(lambda: (s if (s := d.state())['view'] == 'crop'
                           and s['crop_loaded'] else None), timeout=30)
    expected_size = [2448, 3264]
    assert state['camera_angle'] == 270
    assert state['crop_source'] == expected_size, state['crop_source']
    assert state['crop_source_rect'] == [0, 0] + expected_size
    assert not state['crop_busy'] and not state['crop_save_album']
    assert d.shell('cat /sys/kernel/gsensor/report_active') == report_active
    d.capture('camera-orientation-HD-crop')
    directory = d.DATA + '/camera-tmp'
    files = d.shell('find ' + shlex.quote(directory) + ' -maxdepth 1 -type f').splitlines()
    assert len(files) == 1 and files[0].startswith(directory + '/IMG_')
    assert files[0].lower().endswith('.jpg')
    original = d.OUT / 'camera-orientation-HD-original.jpg'
    subprocess.run(['adb', '-s', d.SERIAL, 'pull', files[0], str(original)],
                   check=True, capture_output=True)
    actual_size = jpeg_size(original.read_bytes())
    assert actual_size == expected_size, actual_size
    bounds = state['crop_actions']['cancel']
    d.tap((bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)
    d.wait(lambda: d.state()['view'] == 'camera')
    d.wait(lambda: d.state()['camera_ready'], timeout=20)
    # Use the camera's explicit Back control; this scoped flow verifies camera
    # cleanup, while edge-swipe navigation has its own acceptance fixture.
    safe = d.state()['safe_insets']
    d.tap(safe[0] + 24, safe[1] + 24)
    state = d.wait(lambda: (s if (s := d.state())['view'] == 'chat' else None))
    d.wait(lambda: d.shell('cat /sys/kernel/gsensor/report_active') == report_active)
    assert state['session'] == session and state['draft_length'] == draft
    assert len(state['chat_images']) == image_count and not state['generating']
    assert not d.shell('find ' + shlex.quote(directory) + ' -maxdepth 1 -type f')
    assert album_inventory() == album_before
    assert d.pid() == native
    d.result['camera_orientation'] = {
        'poses': poses, 'hd_jpeg_size': actual_size, 'crop_cancelled': True,
        'system_album_unchanged': True, 'sensor_reporting_restored': True,
        'source_data_isolated_by_parent': True,
        'four_pose_optical_target': 'pending manual physical rotation',
    }
