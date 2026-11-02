"""Touch only actual onboarding controls, without a production test backdoor."""
import test_lvgl_app as t

def action(app,name):
 bounds=app.diag()['onboarding']['actions'][name]
 app.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
 t.wait_for(lambda:not (app.diag().get('onboarding') or {}).get('busy',False))
