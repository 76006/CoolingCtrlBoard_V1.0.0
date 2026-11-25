# 冷媒罐循环试验实施计划

依据同目录上级 specs 中的 2026-11-24-relief-cycle-design.md。使用 Windows Qt 6.5.3 Widgets、SerialPort 与 STM32F302 RT-Thread 5.2.2。测试固件上电停热，每份 ADC 快照至多参与一次判定。

## 分工与验证

- [x] 在 applications/pressure/relief_cycle.{h,c} 实现可在主机编译的定点状态机。用 C 测试回放完整两轮、重复泄压、未确认温降、未泄压超时、回温中断、初压无效和压力上限。
- [x] 在 applications/pressure 下接入固件试验服务与 FinSH 接口，修改 ADC 换算、main、现有压力线程与安全裁决。每轮加热必须经过状态机许可，串口发送使用独立线程。编译测试模式和正常模式。
- [x] 在 Cooling_TestTool_Qt 实现串口握手、配置与应答、运行与中断恢复、逐事件显示与 CSV 保存。核对限值、首次触发压力、事件重发和数据丢失。
- [x] 实现双轴曲线、回放与整段 PNG 导出。编译、打包并离屏验证桌面程序，提供 USB 转 TTL 接线与启动说明。

## 版本 1 串口约定

115200 8N1。PC 发送 ASCII 命令并以 CR 结束（FinSH），设备返回独立 ASCII 行，以 @R, 开始。十进制整数，压力 0.001 MPa、温度 0.1 ℃、配置时长单位 100 ms、采样时间为设备毫秒。run 为 PC 生成的 1..2147483647 随机数。在线空闲状态也提供遥测。会话心跳每 0.5 秒，板端 3 秒超时。以下列出字段顺序。

    rc hello                     -> @R,H,1,hardPressure,run,state
    rc cfg RUN CFG               -> @R,C,RUN,CFG
    rc sync RUN                  -> @R,Y,RUN,acknowledged,next_event
    rc start RUN                 -> @R,A,RUN,START
    rc stop RUN                  -> @R,A,RUN,STOP
    rc ping RUN ACKID            -> 刷新心跳，确认已持久保存的连续事件
    错误                         -> @R,X,RUN,REASON
    采样                         -> @R,S,run,adcSeq,ms,round,state,p,t0,t1,valid,heat
    事件，直到确认反复发送         -> @R,E,run,eventId,ms,round,kind,p,t0,t1,peak,stage,valid

CFG 为单个逗号分隔参数，依次是最高压力、初始压力（-1 自动）、容差、温降窗口、降幅、开始确认、回升幅度、停止确认、稳定时间、达上限观察时长、单轮时限、轮数。固件范围依次为 1..hardPressure-1、-1 或 0..hardPressure-1、1..999、1..100、1..1000、1..600、1..1000、1..600、1..600、1..6000、1..864000、1..999。有效位按 bit0 NTC0、bit1 NTC1、bit2 压力。协议版本 1 固定上述范围，硬截止值来自握手。

状态字为 IDLE BASE HEAT LIMIT CHECK VENT RETURN DONE ABORT。事件字为 BASE（取得初始值）、HEAT、LIMIT、TRIGGER、OPEN（使用候选时刻与读数）、CLOSE、ROUND（最终峰值）、DONE、FALSE_DROP、NO_RELIEF、TIMEOUT、BAD_BASE、STOP、LINK、SENSOR、INTERLOCK、OVERPRESS、OVERTEMP、OVERFLOW。异常事件使本次试验结束。CURRENT run 保留到下一次接收新试验。采样可能在通讯中缺失，事件必须去重并连续确认。

## 曲线接口

Cooling_TestTool_Qt/curveplot.h 暴露 CurvePlot(QWidget *parent=nullptr)、clear()、addSample(quint32 seq, quint32 ms, int p, int t0, int t1, int valid)、addEvent(quint32 ms,const QString &label)、bool savePng(const QString &path)。运行中窗口显示最近片段，导出覆盖所有数据。启动之前、停止之后都允许串口遥测，导出和归档严格按试验隔离。
