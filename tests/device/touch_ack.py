"""A fresh automation tap may follow a proven, wholly unobserved lease cancellation."""
def retry_unseen_tap(before,after,allow_cancelled_down=False):
 if before.get('pid')!=after.get('pid'):return False
 if before.get('touch',{}).get('gesture')!=after.get('touch',{}).get('gesture') and not (allow_cancelled_down and after.get('touch',{}).get('pressed')):return False
 if after.get('touch',{}).get('pressed',False) and not allow_cancelled_down:return False
 if not all(after.get(k) for k in ('visible','page_visible','window_focused','input_focused')):return False
 if after.get('keyboard') or after.get('system_overlay',{}).get('panel_visible'):return False
 for key in ('view','session','active_model','generating','keyboard','modal','navigation_open','attachments','composer_text_bounds','composer_hidden','draft_length','reply_actions','message_interactions','provider_workspace','transfer_screen','transfer_data','scroll_y','scroll_bottom','text_selection'):
  if before.get(key)!=after.get(key):return False
 a,b=before.get('editor',{}),after.get('editor',{})
 for key in ('session','active','layout','layout_menu','layout_hold','preedit_length','length','revision','actions','pending','candidate_count','cursor','selection_start','selection_end','text_scroll','clipboard_count'):
  if a.get(key)!=b.get(key):return False
 if before.get('context_menu')!=after.get('context_menu'):return False
 if before.get('settings',{}).get('rows')!=after.get('settings',{}).get('rows'):return False
 a,b=before.get('onboarding') or {},after.get('onboarding') or {}
 if any(a.get(k)!=b.get(k) for k in ('step','paused','actions')):return False
 start=before.get('diagnostic_time',float('inf'));expired=False
 for row in after.get('input_focus_trace',{}).get('entries',[]):
  if row['time']<start:continue
  if row['reason']=='window_focus_received_before_apply' and not row['event_value']:return False
  if row.get('overlay',{}).get('panel_visible'):return False
  if row['reason']=='input_focus_applied' and not row['event_value']:return False
  if row['reason']=='touch_lease_applied_before_cancel' and not row['event_value']:
   if not (row.get('page_visible') and row.get('window_focused') and row.get('focus_active') and 300<=row.get('lease_age_ms',0)<1500):return False
   expired=True
 return expired
