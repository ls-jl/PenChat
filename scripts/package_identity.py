"""Application identity comes from package.json; distribution suffix names the native build variant."""
TARGETS = ('rk3562', 'cv1826-s')


def amr_name(app, target=None):
    if target is not None and target not in TARGETS:
        raise ValueError('Unknown native target: ' + target)
    suffix = '-' + target if target else ''
    return app['appid'] + '.' + app['version'].replace('.', '_') + suffix + '.amr'
