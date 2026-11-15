const test = require('node:test')
const assert = require('node:assert/strict')
const vm = require('node:vm')
const { execFileSync } = require('node:child_process')

// Importing this host generator never invokes main(), adb or a device fixture.
const page = execFileSync('python3', ['-c',
  "import runpy; print(runpy.run_path('tests/device/update_selftest.py')['fixture_page']('0.0.2','host-regression',\"this.message='updated'\"))"], { encoding: 'utf8' })
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1]
  .replace(/^import .*$/gm, '').replace('export default', 'module.exports =')

function fixture(mkdir) {
  const timers = new Map(), calls = []
  let id = 0
  const context = vm.createContext({
    fs: { mkdir }, module: {}, launcher: {},
    setTimeout: (fn, delay) => { timers.set(++id, { fn, delay }); return id },
    clearTimeout: key => timers.delete(key),
  })
  vm.runInContext(script, context)
  const options = context.module.exports
  const component = { ...options.data(), $page: { finish: () => calls.push('finish') } }
  return { component, options, calls, timers }
}

for (const failure of ['reject', 'hang', 'throw']) {
  test(`owned fixture finishes when mounted/finish marker ${failure}`, async () => {
    const events = []
    let instance
    instance = fixture(path => {
      // The timer must exist already at the first SDK call.
      assert.equal(instance.timers.size, 1)
      events.push(path.split('/').pop())
      if (failure === 'reject') return Promise.reject(new Error('SDK rejected'))
      if (failure === 'throw') throw new Error('SDK threw')
      return new Promise(() => {})
    })
    const mounting = instance.options.mounted.call(instance.component)
    const timer = instance.timers.get(instance.component.finishTimer)
    assert.equal(timer.delay, 15000)
    timer.fn()
    // Neither rejecting nor permanently pending promises delay the close call.
    assert.deepEqual(instance.calls, ['finish'])
    assert.deepEqual(events, ['mounted', 'finish-requested'])
    if (failure !== 'hang') {
      await mounting
      await new Promise(resolve => setImmediate(resolve))
      assert.match(instance.component.message, /FIXTURE ERROR/)
      assert.ok(instance.component.fixtureError)
    }
  })
}

test('unload marker rejection is reported and does not reject lifecycle hook', async () => {
  const instance = fixture(() => Promise.reject(new Error('SDK rejected')))
  await instance.options.onUnload.call(instance.component)
  assert.equal(instance.component.fixtureError, 'unload-marker')
  assert.equal(instance.component.message, 'FIXTURE ERROR unload-marker')
})

test('successful diagnostics retain normal finish and unload', async () => {
  const events = []
  const instance = fixture(async path => { events.push(path.split('/').pop()); return true })
  await instance.options.mounted.call(instance.component)
  assert.equal(instance.component.message, 'updated')
  instance.timers.get(instance.component.finishTimer).fn()
  assert.deepEqual(instance.calls, ['finish'])
  await instance.options.onUnload.call(instance.component)
  assert.deepEqual(events, ['mounted', 'finish-requested', 'unloaded'])
  assert.equal(instance.timers.size, 0)
})
