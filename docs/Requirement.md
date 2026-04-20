这是一个面向儿童的AI语音工具，支持wifi和4G网络连接
主页有状态栏和内容区，其中状态栏在最上方，可以显示wifi/4g连接图像，图像可以具体的wifi/4g的信号强度变化。状态栏最右方显示电池信息，显示当前电池使用的状况，状态栏中间显示时间或者跟AI对话的状态信息。

内容区包含12个图标，每屏可以展示6个，分两屏，通过横向滑动切换这两个屏幕显示。这12个图标的图标文件在C:\code\xiaozhi-esp32-zzx\docs\pic\icon目录下。每个菜单下使用的图片背景在C:\code\xiaozhi-esp32-zzx\docs\pic\background下。进入菜单后，使用半圆形的箭头按键返回主页面。

12个图标对应的需求如下：
1、AI闹钟：可语音设置定时，并显示倒计时界面，可后台运行到时间后语音提示5次
如：15分钟后提醒醒我上课；	

2、百科问答：对接LLM（如豆包/千问）的智能体，回答各种各样的百科知识问题，包括十万个为什么等，充当百科知识官的角色，点开百科问答后，屏幕背景是"C:\code\xiaozhi-esp32-zzx\docs\pic\background\百科问答.png"，同时播放 “百科问答 有问必答 我什么都懂哦 请尽管提问”，同时屏幕下方有个10秒倒计时，如果计时器归零还没有捕捉到有人提问，则播放“没有问题我就退下哦，随时等你哦”，然后返回主页面。

3、口语陪练：对接LLM（如豆包/千问）的智能体，通过英语语音交互，协助儿童训练口语，充当陪练英语的角色；

4、拍识万物：可语音发问拍摄到的东西是什么，或点击屏幕上的图像获得LLM反馈，支持多语言；

5、睡前故事：对接LLM（如豆包/千问）的智能体，通过语音交互跟儿童讲睡前故事等；

6、遥控自拍：本功能可用自然语言命令设备通过蓝牙，控制手机相机界面需提醒用户先用蓝牙连手机，再操作；

7、AI打印：拍照后根据所选模式，如（动漫、卡通......），将照片发往大模型处理后，可选打印。

8、单词听写：可根据语音描述选择教材的章节进行教材内容听写；

9、看相：对接LLM，通过摄像头拍到的面相、手相、骨相、声相、舌像等作出推算（娱乐性质）；

10、魔镜美妆：打开摄像头后可根据镜头内的人脸妆容提出建议、赞美；

11、助眠音乐：设计音乐播放页面，本地存储几首雨声、白嗓音等，可定时播完休眠；

12、设置：WiFi/4G点击切换，服务器切换，语言切换，显示关键信息等等



拍识万物：点击AI按键按钮没有作用
遥控自拍：点击拍照可以，语音没反应，页面又回到初版那样了
听写      ：听写内容页面显示只有9年级下，不完整。

Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.

Core  0 register dump:
PC      : 0x4211beb2  PS      : 0x00060730  A0      : 0x82119ce0  A1      : 0x3fcc1360  
--- 0x4211beb2: os_memblock_put at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/nimble/src/os_mempool.c:650
A2      : 0x00000010  A3      : 0x3c4d570c  A4      : 0x00000002  A5      : 0x00000000  
A6      : 0x00000000  A7      : 0x00000000  A8      : 0x00000000  A9      : 0x00000000  
A10     : 0x00000010  A11     : 0x3c4d570c  A12     : 0x00000002  A13     : 0x3fcc1320  
A14     : 0x3fcb6074  A15     : 0x00000000  SAR     : 0x00000018  EXCCAUSE: 0x0000001c  
EXCVADDR: 0x0000001a  LBEG    : 0x400570e8  LEND    : 0x400570f3  LCOUNT  : 0x00000000  
--- 0x400570e8: memset in ROM
--- 0x400570f3: memset in ROM

Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.

Core  0 register dump:
PC      : 0x4211d476  PS      : 0x00060730  A0      : 0x8211b2a4  A1      : 0x3fccb2a0  
--- 0x4211d476: os_memblock_put at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/nimble/src/os_mempool.c:650
A2      : 0x00000010  A3      : 0x3c3da178  A4      : 0x00000002  A5      : 0x00000001  
A6      : 0x00000000  A7      : 0x00000000  A8      : 0x00000000  A9      : 0x00000000  
A10     : 0x00000010  A11     : 0x3c3da178  A12     : 0x00000002  A13     : 0x00000000  
A14     : 0x00000000  A15     : 0x00000000  SAR     : 0x00000008  EXCCAUSE: 0x0000001c  
EXCVADDR: 0x0000001a  LBEG    : 0x400570e8  LEND    : 0x400570f3  LCOUNT  : 0x00000000  
--- 0x400570e8: memset in ROM
--- 0x400570f3: memset in ROM


Backtrace: 0x4211d473:0x3fccb2a0 0x4211b2a1:0x3fccb2c0 0x421194da:0x3fccb2e0 0x42119574:0x3fccb410 0x42114f43:0x3fccb540 0x421152f5:0x3fccb570 0x421145c5:0x3fccb590 0x421f52a7:0x3fccb5b0 0x4037bc19:0x3fccb5d0 0x4203acde:0x3fccb5f0
--- 0x4211d473: os_memblock_put at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/nimble/src/os_mempool.c:626
--- 0x4211b2a1: ble_gatts_connection_broken at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_gatts.c:1698
--- 0x421194da: ble_gap_conn_broken at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c:1762
--- 0x42119574: ble_gap_rx_disconn_complete at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c:1848
--- 0x42114f43: ble_hs_hci_evt_disconn_complete at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_hci_evt.c:553
--- (inlined by) ble_hs_hci_evt_disconn_complete at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_hci_evt.c:440
--- 0x421152f5: ble_hs_hci_evt_process at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_hci_evt.c:1835
--- 0x421145c5: ble_hs_event_rx_hci_ev at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_hs.c:655
--- 0x421f52a7: npl_freertos_event_run at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/npl/freertos/src/npl_os_freertos.c:536
--- 0x4037bc19: ble_npl_event_run at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/npl/freertos/include/nimble/nimble_npl_os.h:185
--- (inlined by) nimble_port_run at /home/hao/esp/esp-idf/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c:422
--- 0x4203acde: (anonymous namespace)::BleHidHostTask(void*) at /home/hao/projects/xiaozhi-esp32-zzx-2/components/ble_hid/ble_hid.cc:171

