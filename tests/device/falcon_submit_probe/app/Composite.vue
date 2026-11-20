<template>
  <div :style="pageStyle">
    <hole v-if="live" :style="holeStyle"></hole>
    <div :style="headerStyle"><text :style="labelStyle">{{ message }}</text></div>
    <div :style="buttonStyle(0)" @click="run"><text :style="labelStyle">开始合成</text></div>
    <div :style="buttonStyle(1)" @click="stop"><text :style="labelStyle">停止</text></div>
    <div :style="buttonStyle(2)" @click="disconnect"><text :style="labelStyle">断开进程</text></div>
    <div :style="buttonStyle(3)" @click="exit"><text :style="labelStyle">退出</text></div>
  </div>
</template>
<script>
import { probe } from 'penchatSubmitProbe'
import am from '$am'
function box(x,y,w,h,color) {return {position:'absolute',left:x+'px',top:y+'px',width:w+'px',height:h+'px',backgroundColor:color}}
export default {
  data() {const s=$falcon.$app.probeScreen;return {width:s.width,height:s.height,live:false,message:'共享内存合成测试 · 系统控制条'}},
  computed: {
    pageStyle() {return box(0,0,this.width,this.height,'#20252c')},
    holeStyle() {return box(0,0,this.width,this.height,'transparent')},
    headerStyle() {return Object.assign(box(0,0,this.width,46,'#283747'),{justifyContent:'center',paddingLeft:'16px'})},
    labelStyle() {return {fontSize:'18px',color:'#ffffff'}}
  },
  mounted() {
    this._visible=true;this._generation=0;this._closed=false
    this._focus=()=>this.syncFocus(true)
    try {am.on('topApp',this._focus);am.on('windowAttachState',this._focus)}catch(error){}
    this._poll=setInterval(()=>{
      this.syncFocus(false)
      try {
        const status=probe.compositeStatus();this._stats=status
        if(this.live&&!status.running){this.live=false;this.message='已恢复系统：'+status.reason+' · '+status.output_frames+'帧'}
      }catch(error){this.stop()}
    },100)
    this._auto=setTimeout(()=>this.run(),600)
  },
  beforeDestroy() {this.dispose()},
  methods: {
    buttonStyle(i) {const w=Math.floor((this.width-40)/4);return Object.assign(box(8+i*(w+8),this.height-48,w,44,'#39424f'),{justifyContent:'center',alignItems:'center'})},
    async run() {
      if(this._starting||this.live||!this._visible||this._closed)return
      this._starting=true
      const generation=++this._generation
      this.message='正在准备独立原生进程…'
      try {
        await probe.startComposite()
        if(generation!==this._generation||this._closed||!this._visible){probe.stopComposite();return}
        this.live=true;this.message='系统层在上 · LVGL共享内存在下'
        this.syncFocus(true)
      }catch(error){this.live=false;this.message='未接入：'+String(error)}
      finally{this._starting=false}
    },
    syncFocus() {
      let active=false
      try{active=this.live&&this._visible&&!this._closed&&am.hasWindowFocus()===true}catch(error){}
      try{probe.focusComposite(active)}catch(error){}
    },
    stop() {
      ++this._generation;this.live=false;clearTimeout(this._auto)
      try{const s=probe.stopComposite();this._stats=s;this.message='已恢复系统 · 输出'+s.output_frames+'帧'}catch(error){}
    },
    disconnect() {try{probe.disconnectComposite()}catch(error){}},
    onHide() {this._visible=false;this.stop()},
    onShow() {this._visible=true;this.syncFocus()},
    onUnload() {this.dispose()},
    dispose() {
      this._closed=true;this.stop();clearInterval(this._poll)
      try{am.off('topApp',this._focus);am.off('windowAttachState',this._focus)}catch(error){}
    },
    exit() {this.stop();$falcon.$app.finish()}
  }
}
</script>
