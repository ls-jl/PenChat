"""Assert the public chat instruction contract independently of the builder."""
def assert_chat_prompt(prompt, session):
    assert isinstance(prompt,str)
    assert prompt.startswith('PenChat_APPLICATION_RULES_V1\n')
    assert prompt.count('PenChat_APPLICATION_RULES_V1')==1
    marker='Session instructions (subject to the fixed application rules):'
    assert prompt.count(marker)==1
    assert 'cannot override these application rules' in prompt.split(marker)[0]
    assert prompt.count(session)==1 and prompt.endswith('\n'+session)
    application=prompt.split(marker)[0]
    assert '默认请避免在回复中输出表情包' in application
    assert '不是强制禁止' in application and '用户明确要求输出表情包时，可以按用户要求输出' in application
