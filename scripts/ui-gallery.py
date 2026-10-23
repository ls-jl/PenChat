#!/usr/bin/env python3
"""Build a local before/after gallery from actual LVGL and device captures."""
import html,json,pathlib,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
out=ROOT/'.work/ui-audit';after=out/'after'
cases=[
('聊天：空白','chatgpt-style/empty.png'),('聊天：长草稿','chatgpt-style/long-draft.png'),
('聊天：消息与图片','chatgpt-style/rounded-photo-layout.png'),('聊天：思考展开','reasoning-ui/expanded-summary.png'),
('聊天：表格','chatgpt-style/table-synchronized-scroll.png'),('聊天：代码横向滚动','chatgpt-style/wide-code.png'),
('上拉新建：进度','chatgpt-style/pull-partial.png'),('上拉新建：完成','chatgpt-style/pull-full.png'),
('历史抽屉','reasoning-ui/history-no-header.png'),('历史操作菜单','reasoning-ui/history-actions.png'),
('设置','chatgpt-style/settings.png'),('归档列表','reasoning-ui/archives.png'),('归档：空状态','extra/archives-empty.png'),
('Provider 与模型列表','chatgpt-style/providers.png'),('Provider：新建','extra/provider-new.png'),
('Provider：编辑','extra/provider-edit.png'),('Provider：校验反馈','extra/validation-dialog.png'),
('模型：推理能力','reasoning-ui/model-capabilities.png'),('模型：参数映射','reasoning-ui/model-mapping.png'),
('API Key：键盘 OCR','extra/key-editor.png'),('加号菜单','chatgpt-style/attachments.png'),
('模型选择','reasoning-ui/models.png'),('标题模型选择','extra/title-model-picker.png'),
('推理强度：两档','reasoning-ui/two-levels.png'),('推理强度：多档','reasoning-ui/four-levels.png'),
('思考展示选项','chatgpt-style/thought-display-options.png'),('预设列表与开关','chatgpt-style/presets-settings.png'),
('预设：编辑','extra/preset-editor-top.png'),('预设：提示词编辑入口','extra/preset-editor-bottom.png'),
('预设选择','chatgpt-style/presets-picker.png'),('预设：提示词选项','extra/preset-prompt-options.png'),
('预设：模型选项','extra/preset-model-options.png'),('预设：强度选项','extra/preset-effort-options.png'),
('相机','chatgpt-style/camera.png'),('裁剪','chatgpt-style/camera-crop.png'),
('裁剪：90°','chatgpt-style/photo-zoom-90.png'),('裁剪：180°','chatgpt-style/photo-zoom-180.png'),('裁剪：270°','chatgpt-style/photo-zoom-270.png'),
('相册：空状态','chatgpt-style/album.png'),('相册：照片','extra/album-populated.png'),
('待发送图片：删除确认','chatgpt-style/delete-image-confirm.png'),('扫描：采集中','scanner-ui/capturing.png'),
('扫描：完整结果','scanner-ui/result.png'),('扫描：部分结果','scanner-ui/partial.png'),('扫描：无结果','scanner-ui/empty.png'),
('首次使用','extra/welcome-unconfigured.png'),('模型列表：空状态','extra/providers-empty.png'),
('系统输入法（保留系统 UI）','device/system-ime.png'),('启动器：错误与重试','device/launcher-error.png')]
for angle in [0,90,180,270]:
    path=f'device/camera-{angle}.png'
    (after/'device').mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/f'.work/device-acceptance/camera-orientation-{angle}.png',after/path)
    cases.append((f'相机：真机 {angle}°',path))
rows=[]
for title,path in cases:
    target=after/path
    if not path.startswith(('extra/','device/')):
        source=ROOT/'.work'/path
        assert source.exists(),source
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    assert target.exists(),target
    figures=[]
    for stage,label in [('before','改版前'),('after','改版后')]:
        image=out/stage/path
        if image.exists():
            src=html.escape(stage+'/'+path,quote=True)
            figures.append(f'<figure><figcaption>{label}</figcaption><a href="{src}" target="_blank"><img src="{src}" loading="lazy" alt="{html.escape(title)}"></a></figure>')
        else:figures.append('<figure><figcaption>未留存改版前截图</figcaption><p>此补充状态以改版后截图检查。</p></figure>')
    rows.append(f'<section data-title="{html.escape(title,quote=True)}"><h2>{html.escape(title)}</h2><div class="pair">'+''.join(figures)+'</div></section>')
version=json.loads((ROOT/'package.json').read_text())['version']
(out/'index.html').write_text('''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PenChat UI 逐页检查</title>
<style>body{background:#171717;color:#ececec;font:16px system-ui;margin:0;padding:28px}header{max-width:1200px;margin:auto}h1{font-size:28px}p,figcaption{color:#b4b4b4}input{background:#303030;color:white;border:1px solid #555;border-radius:10px;padding:12px;width:300px;max-width:80%}section{margin:28px auto;max-width:2000px}h2{font-size:19px}figure{margin:0;min-width:0;background:#212121;padding:12px;border-radius:12px}figcaption{margin-bottom:12px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}img{width:100%;height:auto} @media(max-width:1000px){.pair{grid-template-columns:1fr}}[hidden]{display:none!important}</style>
<header><h1>PenChat '''+version+''' · UI 逐页检查</h1><p>真实 LVGL 渲染与设备截图；全部使用测试内容。点击图片可查看原始尺寸。原生画面为 960 × 266。</p><input placeholder="筛选页面，例如：预设、相册" oninput="document.querySelectorAll('section').forEach(s=>s.hidden=!s.dataset.title.includes(this.value))"></header>'''+''.join(rows)+'</html>')
(out/'manifest.json').write_text(json.dumps({'version':version,'states':len(cases),'pages':[{'title':t,'path':p} for t,p in cases]},ensure_ascii=False,indent=2)+'\n')
print(f'{len(cases)} reviewed UI states: {out / "index.html"}')
