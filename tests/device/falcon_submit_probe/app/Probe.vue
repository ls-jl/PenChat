<template>
  <div :style="pageStyle">
    <text :style="titleStyle">S6 原生提交透传测试</text>
    <text :style="statusStyle">{{ message }}</text>
    <div :style="movingStyle"></div>
    <text :style="counterStyle">{{ tick }}</text>
    <div :style="buttonStyle(0)" @click="run"><text :style="labelStyle">运行测试</text></div>
    <div :style="buttonStyle(1)" @click="stop"><text :style="labelStyle">立即撤回</text></div>
    <div :style="buttonStyle(2)" @click="exit"><text :style="labelStyle">退出</text></div>
  </div>
</template>
<script>
import { probe } from 'penchatSubmitProbe'
function box(x, y, w, h, color) {
  return {position:'absolute', left:x+'px', top:y+'px', width:w+'px', height:h+'px', backgroundColor:color}
}
export default {
  data() {
    const screen = $falcon.$app.probeScreen
    return {width:screen.width, height:screen.height, message:'只观察并调用原函数；不启用原生视频层', tick:0}
  },
  computed: {
    pageStyle() {return box(0,0,this.width,this.height,'#20252c')},
    titleStyle() {return Object.assign(box(16,8,this.width-32,30,'transparent'),{color:'#ffffff',fontSize:'20px'})},
    statusStyle() {return Object.assign(box(16,44,this.width-32,30,'transparent'),{color:'#b5c4d8',fontSize:'16px'})},
    movingStyle() {return box(16+(this.tick*9)%Math.max(1,this.width-84),88,48,48,'#61b99c')},
    counterStyle() {return Object.assign(box(16,148,this.width-32,26,'transparent'),{color:'#ffffff',fontSize:'18px'})},
    labelStyle() {return {color:'#ffffff',fontSize:'18px'}}
  },
  mounted() {
    this._generation=0; this._hidden=false
    this._timer=setInterval(()=>{if(!this._hidden)this.tick++},50)
    this._auto=setTimeout(()=>this.run(),800)
  },
  beforeDestroy() {this.dispose()},
  methods: {
    buttonStyle(i) {
      const width=Math.floor((this.width-48)/3)
      return Object.assign(box(16+i*(width+8),this.height-60,width,44,'#39424f'),{justifyContent:'center',alignItems:'center'})
    },
    async run() {
      if(this._running||this._hidden)return
      this._running=true
      const generation=++this._generation
      try {
        this.message='阶段1：原样透传'
        await probe.start({copy:false})
        if(generation!==this._generation){probe.stop();return}
        this._phase=setTimeout(async()=>{
          try {
            if(generation!==this._generation)return
            probe.stop()
            this.message='阶段2：提交前读取，最多30帧'
            await probe.start({copy:true})
            if(generation!==this._generation){probe.stop();return}
            this._phase=setTimeout(()=>{
              if(generation!==this._generation)return
              const result=probe.stop()
              this.message='已撤回：'+result.samples+'帧，读取中位'+Number(result.copy_median_ms).toFixed(3)+'ms'
              this._running=false
            },4500)
          }catch(error){this.message='测试停止：'+String(error);this.stop()}
        },3500)
      }catch(error){this.message='未接入：'+String(error);this.stop()}
    },
    stop() {
      ++this._generation;this._running=false
      clearTimeout(this._phase);clearTimeout(this._auto)
      try {probe.stop()}catch(error){}
    },
    onHide() {this._hidden=true;this.stop()},
    onShow() {this._hidden=false},
    onUnload() {this.dispose()},
    dispose() {this.stop();clearInterval(this._timer)},
    exit() {this.stop();$falcon.$app.finish()}
  }
}
</script>
