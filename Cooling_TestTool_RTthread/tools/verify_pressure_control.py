"""Exercise the compiled ARM firmware with mocked hardware/RTOS calls.

Usage: python tools/verify_pressure_control.py Debug/rtthread.elf --mode test
Requires the Python unicorn package. No board is accessed.
"""

import argparse
import struct
from fractions import Fraction
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP


class Firmware:
    STOP = 0x0803FFF0
    SCRATCH = 0x20007000

    def __init__(self, path):
        data = Path(path).read_bytes()
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        assert header[0][:5] == b"\x7fELF\x01", "ELF32 required"
        sections = [struct.unpack_from("<10I", data, header[6] + i * header[11])
                    for i in range(header[12])]
        self.symbols = {}
        for section in sections:
            if section[1] != 2:  # SHT_SYMTAB
                continue
            strings_section = sections[section[6]]
            strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
            for offset in range(section[4], section[4] + section[5], section[9]):
                name, value, size, _, _, _ = struct.unpack_from("<IIIBBH", data, offset)
                name = strings[name:strings.find(b"\0", name)].decode()
                self.symbols[name] = (value, size)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x08000000, 0x40000)
        self.uc.mem_map(0x20000000, 0x10000)
        for section in sections:
            if section[1] == 1 and section[2] & 2 and section[5]:
                self.uc.mem_write(section[3], data[section[4]:section[4] + section[5]])
        self.tick = 1000
        self.age = 0
        self.raw = self.filtered = self.adc(600)
        self.adc_seq = 0
        self.ntc0_adc = self.ntc1_adc = 2048  # 10k NTC at 25 C
        self.valid_mask = 7
        self.temperature = 200
        self.temperature_valid = True
        self.output = False
        self.control_started = False
        self.stream_context = None
        self.wire = []
        self.reason = None
        mocks = {
            "rt_mutex_init": lambda: 0,
            "rt_mutex_take": lambda: 0,
            "rt_mutex_release": lambda: 0,
            "rt_thread_create": lambda: self.SCRATCH,
            "rt_thread_startup": lambda: 0,
            "rt_thread_delete": lambda: 0,
            "rt_tick_get": lambda: self.tick,
            "rt_kprintf": lambda: 0,
            "rt_kputs": self.serial_output,
            "finsh_get_echo": lambda: 1,
            "finsh_get_prompt_mode": lambda: 1,
            "finsh_set_echo": lambda: 0,
            "finsh_set_prompt_mode": lambda: 0,
            "cooling_board_heater_init": self.heater_init,
            "cooling_board_heater_write": self.heater_write,
            "cooling_adc_start": lambda: 0,
            "cooling_tank_start": lambda: 0,
            "cooling_can_start": lambda: -1,  # No CAN device or master required.
            "cooling_adc_get_snapshot": self.adc_snapshot,
            "cooling_sensor_ntc1_temperature_deg10": self.ntc_temperature,
        }
        self.mocks = {self.symbols[name][0] & ~1: callback for name, callback in mocks.items()
                      if name in self.symbols}
        self.uc.hook_add(UC_HOOK_CODE, self.hook)
        # Simulate the board UID check succeeding before main starts.
        address, size = self.symbols["is_encry_ok"]
        self.uc.mem_write(address, (1).to_bytes(size, "little"))
        self.call("main")

    @staticmethod
    def adc(pressure):
        points = ((0, 474), (700, 2860), (790, 3038), (1200, 4471))
        for (p0, mv0), (p1, mv1) in zip(points, points[1:]):
            if pressure <= p1 or p1 == points[-1][0]:
                sensor_mv = mv0 + Fraction((pressure - p0) * (mv1 - mv0), p1 - p0)
                return int(sensor_mv * Fraction(4586 * 4095, 10000 * 3000) + Fraction(1, 2))

    def heater_init(self):
        self.output = False
        return 0

    def heater_write(self):
        self.output = bool(self.uc.reg_read(UC_ARM_REG_R0))
        return 0

    def adc_snapshot(self):
        snapshot = struct.pack("<IIBB9H", self.adc_seq, self.tick - self.age, self.valid_mask,
                               int(self.output),
                               self.ntc0_adc, self.ntc0_adc, self.ntc0_adc * 3000 // 4095,
                               self.ntc1_adc, self.ntc1_adc, self.ntc1_adc * 3000 // 4095,
                               self.raw, self.filtered, self.filtered * 3000 // 4095)
        self.uc.mem_write(self.uc.reg_read(UC_ARM_REG_R0), snapshot)
        return 0

    def ntc_temperature(self):
        self.uc.mem_write(self.uc.reg_read(UC_ARM_REG_R0), struct.pack("<h", self.temperature))
        return 0 if self.temperature_valid else -1

    def serial_output(self):
        p = self.uc.reg_read(UC_ARM_REG_R0)
        line = bytes(self.uc.mem_read(p, 128)).split(b"\0", 1)[0].strip().decode("ascii")
        if line.startswith("@R,"):
            self.wire.append(line)
        return 0

    def hook(self, uc, address, size, user_data):
        if address == self.STOP:
            self.reason = "return"
            uc.emu_stop()
        elif address == self.symbols["rt_thread_mdelay"][0] & ~1:
            uc.reg_write(UC_ARM_REG_R0, 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
            self.reason = "cycle"
            uc.emu_stop()
        elif address in self.mocks:
            result = self.mocks[address]()
            uc.reg_write(UC_ARM_REG_R0, result & 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        context = self.uc.context_save()
        self.uc.reg_write(UC_ARM_REG_SP, 0x20009000)
        self.uc.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        for register, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.uc.reg_write(register, value & 0xFFFFFFFF)
        self.reason = None
        self.uc.emu_start(self.symbols[name][0] | 1, 0, count=200000)
        assert self.reason == "return", f"{name} did not return"
        result = self.uc.reg_read(UC_ARM_REG_R0)
        self.uc.context_restore(context)
        return result

    def command(self, name, *args):
        argv = []
        for index, text in enumerate((name,) + args):
            pointer = self.SCRATCH + 0x100 + index * 0x100
            self.uc.mem_write(pointer, text.encode() + b"\0")
            argv.append(pointer)
        self.uc.mem_write(self.SCRATCH, struct.pack("<" + "I" * len(argv), *argv))
        self.call(name, len(argv), self.SCRATCH)

    def targets(self):
        return struct.unpack("<HhI", self.uc.mem_read(self.symbols["pressure_targets"][0], 8))

    def cycle(self, pressure=None, elapsed=50):
        if pressure is not None:
            self.raw = self.filtered = self.adc(pressure)
        self.tick += elapsed
        self.adc_seq += 1
        if not self.control_started:
            self.uc.reg_write(UC_ARM_REG_SP, 0x2000A000)
            self.uc.reg_write(UC_ARM_REG_LR, self.STOP | 1)
            self.uc.reg_write(UC_ARM_REG_PC, self.symbols["pressure_control_entry"][0])
            self.control_started = True
        self.reason = None
        self.uc.emu_start(self.uc.reg_read(UC_ARM_REG_PC) | 1, 0, count=200000)
        assert self.reason == "cycle", "control task did not complete a cycle"
        address, size = self.symbols["pressure_snapshot"]
        # GCC's ARM ABI uses the smallest enum representation in this build.
        assert size == 60, "Update the snapshot layout for this compiler ABI"
        data = self.uc.mem_read(address, size)
        return {"target": struct.unpack_from("<H", data, 22)[0],
                "target_temp": struct.unpack_from("<h", data, 24)[0],
                "state": data[40], "duty": data[41],
                "sufficient": struct.unpack_from("<I", data, 48)[0],
                "low": struct.unpack_from("<I", data, 52)[0],
                "block": struct.unpack_from("<I", data, 56)[0]}

    def stream(self):
        """Run the firmware TX worker cooperatively, with its own ARM stack."""
        previous = self.uc.context_save()
        if self.stream_context is None:
            self.uc.reg_write(UC_ARM_REG_SP, 0x2000D000)
            self.uc.reg_write(UC_ARM_REG_LR, self.STOP | 1)
            self.uc.reg_write(UC_ARM_REG_PC, self.symbols["stream_entry"][0])
        else:
            self.uc.context_restore(self.stream_context)
        self.reason = None
        self.uc.emu_start(self.uc.reg_read(UC_ARM_REG_PC) | 1, 0, count=200000)
        assert self.reason == "cycle", "stream task did not emit a batch"
        self.stream_context = self.uc.context_save()
        self.uc.context_restore(previous)

    def clear_tank_faults(self):
        self.call("cooling_safety_set_tank", 0)
        self.cycle()
        self.call("cooling_safety_set_tank", 1)


def verify_calibration(path, test_mode):
    board = Firmware(path)
    for adc, expected in ((2799, 1200), (0, 0), (297, 0), (1565, 594),
                          (1790, 700), (1791, 701), (1846, 745), (1902, 790),
                          (2162, 909), (2504, 1065), (2867, 1231), (4095, 1600)):
        assert board.call("cooling_sensor_pressure_mpa1000", adc, board.SCRATCH) == 0
        actual, = struct.unpack("<H", board.uc.mem_read(board.SCRATCH, 2))
        assert actual == expected, (adc, actual, expected)
    for pressure, adc in ((0, 297), (700, 1790), (790, 1902), (1000, 2361),
                          (1200, 2799), (1400, 3236), (1600, 3674)):
        assert board.call("cooling_pressure_adc_from_mpa1000", pressure) == adc, pressure
    board.filtered = 1790
    board.raw = 3235
    assert board.cycle()["block"] & (1 << 8) == 0
    board.raw = 3236
    assert board.cycle()["block"] & (1 << 8), "hard cutoff uses raw ADC"
    if test_mode:
        for field in ("raw", "filtered"):
            for adc, valid in ((229, False), (230, True), (3741, True), (3742, False)):
                board.raw = board.filtered = 1790
                setattr(board, field, adc)
                invalid = board.cycle()["block"] & (1 << 6)
                assert bool(invalid) == (not valid), (field, adc)
    print("PASS: pressure calibration points, interpolation, extrapolation and ADC protection thresholds")


def verify_normal(path):
    board = Firmware(path)
    board.call("cooling_safety_set_tank", 1)
    assert board.cycle()["duty"] == 0, "normal boot stays off"
    board.command("heater_power", "on")

    original = board.targets()
    for pressure, temperature in [("0", "60"), ("-1", "60"), ("1.601", "60"),
                                  ("0.825x", "60"), ("0.8251", "60"), ("nan", "60"),
                                  ("0.825", "60.00"), ("0.825", "3276.8"),
                                  ("99999999999999999", "60"), ("", "60")]:
        board.command("heater_target", pressure, temperature)
        assert board.targets() == original, (pressure, temperature)
    board.command("heater_target")
    board.command("heater_target", "0.800", "65.0")
    assert board.targets()[:2] == (800, 650), "serial target update"

    board.temperature = 620
    sample = board.cycle(600)
    assert sample["duty"] == 100 and sample["target_temp"] == 650, sample
    board.temperature = 660
    assert board.cycle(600)["duty"] == 0, "runtime warmup temperature"
    sample = board.cycle(600, elapsed=300050)
    assert sample["low"] and sample["state"] == 3 and sample["duty"] == 0, sample
    board.clear_tank_faults()
    for pressure, temperature in [("1.395", "60"), ("0.825", "69.0")]:
        original = board.targets()
        board.command("heater_target", pressure, temperature)
        assert board.targets() == original, "normal target protection"
    board.command("heater_target", "0.825", "60.0")
    board.temperature_valid = False
    assert board.cycle(600)["block"] & (1 << 7), "normal NTC protection"
    board.temperature_valid = True
    board.temperature = 750
    assert board.cycle(600)["block"] & (1 << 9), "normal overtemperature latch"
    board.temperature = 200
    assert board.cycle(600)["block"] & (1 << 9), "overtemperature stays latched"
    board.clear_tank_faults()
    assert board.cycle(1400)["block"] & (1 << 8), "normal overpressure latch"
    board.command("heater_target", "0.800", "55.0")
    assert board.cycle(600)["block"] & (1 << 8), "target update cannot clear fault"
    board.clear_tank_faults()
    board.command("heater_target", "0.825", "60.0")
    assert board.cycle(830)["duty"] == 0, "original normal stop threshold"
    assert board.cycle(815)["duty"] == 0, "original normal hold band"
    assert board.cycle(800)["duty"] == 20, "original normal reheat threshold"

    board.command("heater_target", "0.825", "60.0")
    board.valid_mask = 0
    sample = board.cycle(600)
    assert sample["block"] & (1 << 6) and sample["duty"] == 0, "missing pressure data"
    board.valid_mask = 7
    board.temperature_valid = True
    board.temperature = 200
    board.age = 501
    assert board.cycle(600)["block"] & (1 << 6), "stale pressure data"
    board.age = 0
    for setter, disabled, enabled, block in [("authorized", 0, 1, 1), ("tank", 0, 1, 5),
                                            ("auto_control", 0, 1, 4), ("ota", 1, 0, 2)]:
        board.call("cooling_safety_set_" + setter, disabled)
        sample = board.cycle(600)
        assert sample["duty"] == 0 and sample["block"] & (1 << block), setter
        board.call("cooling_safety_set_" + setter, enabled)
        assert board.cycle(600)["duty"] > 0, setter + " recovery"
    board.command("heater_power", "off")
    assert not board.output, "serial power off is immediate"
    assert board.cycle()["block"] & (1 << 3)
    board.command("heater_power", "on")
    board.command("heater_power", "on")
    assert board.cycle()["duty"] > 0, "power on is idempotent"
    print("PASS: normal firmware serial targets, boot mode, pressure control, "
          "timeout, temperature behavior and interlocks")


def verify_relief(path):
    board = Firmware(path)
    # The fixture can run with its LS input disconnected.
    assert board.cycle()["duty"] == 0 and not board.output
    # Existing CAN/shell power gates may open, but cannot override test ownership.
    board.command("heater_power", "on")
    assert board.cycle()["duty"] == 0 and not board.output
    assert board.call("cooling_heater_set", 1) != 0 and not board.output

    def command(*args, expect):
        before = len(board.wire)
        board.command("rc", *args)
        assert board.wire[before:] == [expect], (args, board.wire[before:])

    command("hello", expect="@R,H,2,1400,700,0,IDLE")
    run = 42
    cfg = "900,-1,5,2,10,2,5,2,2,10,100,2,600"

    def start():
        run_text = str(run)
        command("cfg", run_text, cfg, expect=f"@R,C,{run},{cfg}")
        command("start", run_text, expect=f"@R,A,{run},START")

    def events():
        # Keep wire-level replay of identical events idempotent.
        result = {}
        for line in board.wire:
            columns = line.split(",")
            if columns[:3] != ["@R", "E", str(run)]:
                continue
            seq = int(columns[3])
            assert len(columns) == 13, columns
            if seq in result:
                assert result[seq] == columns, "replay changed an event"
            result[seq] = columns
        assert sorted(result) == list(range(1, len(result) + 1))
        return list(result.values())

    def capture():
        # The real board's TX thread runs five times per 100 ms ADC period.
        for _ in range(5):
            board.stream()

    def point(p, adc0=2048, state=None, ping=True):
        if ping:
            board.command("rc", "ping", str(run), str(len(events())))
        board.ntc0_adc = adc0
        heat_at_sample = board.output
        control = board.cycle(p, elapsed=100)
        capture()
        frames = [line.split(",") for line in board.wire
                  if line.startswith(f"@R,S,{run},{board.adc_seq},")]
        assert frames, (run, board.adc_seq, board.wire[-3:])
        frame = frames[-1]
        assert len(frame) == 12 and int(frame[4]) == board.tick - board.age, frame
        assert int(frame[11]) == int(heat_at_sample), "sample and output must share a time base"
        if state:
            assert frame[6] == state, (state, frame)
        if frame[6] not in ("HEAT", "MANUAL"):
            assert control["duty"] == 0 and not board.output, (control, frame)
        return control, frame

    def baseline():
        control, frame = point(300, state="HEAT")
        assert control["target"] == 900 and control["duty"] > 0 and board.output
        assert int(frame[10]) == 7

    def ack():
        board.command("rc", "ping", str(run), str(len(events())))

    command("start", "41", expect="@R,X,41,RUN")
    command("cfg", str(run), cfg + ",77", expect=f"@R,X,{run},HEAT_MODE")
    start()
    baseline()
    for round_number in (1, 2):
        # Retransmitted START must not reset the pressure or detector state.
        command("start", str(run), expect=f"@R,A,{run},START")
        point(800, state="HEAT")
        point(900, state="LIMIT")
        point(856, 2300, "CHECK")
        trigger_tick = board.tick
        point(810, 2300, "CHECK")
        point(700, 2300, "VENT")
        open_event = [e for e in events() if e[6] == "OPEN"][-1]
        assert (int(open_event[4]), int(open_event[5])) == (trigger_tick, round_number)
        assert int(open_event[7]) == 856 and int(open_event[10]) == 900, open_event
        assert open_event[11:] == ["VENT", "7"], open_event
        _, cold = point(300, 4030, "VENT")  # outlet near -60 C, electrically valid
        assert int(cold[8]) < -400 and int(cold[10]) == 7
        point(300, 2048, "VENT")
        point(300, 2048, "VENT")
        point(300, 2048, "RETURN")
        point(300, 2048, "RETURN")
        point(300, 2048, "HEAT" if round_number == 1 else "DONE")
    kinds = [e[6] for e in events()]
    assert kinds.count("OPEN") == kinds.count("CLOSE") == kinds.count("ROUND") == 2
    assert kinds[-1] == "DONE"
    cmd_run = str(run + 1)
    command("cfg", cmd_run, cfg, expect=f"@R,X,{cmd_run},PENDING")
    # Simulate a reconnect with a lost final event ACK.
    board.tick += 600
    command("hello", expect=f"@R,H,2,1400,700,{run},DONE")
    capture()
    assert sum(f",{run},{len(events())}," in line and ",DONE," in line
               for line in board.wire if line.startswith("@R,E,")) >= 2
    ack()

    for fault in ("NTC0_OPEN", "NTC1_OPEN", "PRESSURE_OPEN", "STALE",
                  "INTERLOCK", "OVERTEMP", "OVERPRESS", "LINK", "STOP"):
        run += 1
        start()
        baseline()
        if fault == "NTC0_OPEN":
            _, frame = point(600, 4095, "ABORT")
            assert int(frame[10]) & 1 == 0, "open NTC0"
        elif fault == "NTC1_OPEN":
            board.ntc1_adc = 4095
            _, frame = point(600, state="ABORT")
            assert int(frame[10]) & 2 == 0, "open NTC1"
            board.ntc1_adc = 2048
        elif fault == "PRESSURE_OPEN":
            board.raw = 0
            _, frame = point(None, state="ABORT")
            assert int(frame[10]) & 4 == 0, "invalid raw pressure with valid filtered reading"
        elif fault == "STALE":
            board.age = 501
            _, frame = point(600, state="ABORT")
            assert int(frame[10]) == 0, "stale snapshot"
            board.age = 0
        elif fault == "INTERLOCK":
            # An interlock tripped between two 50 ms pressure updates.
            board.call("cooling_safety_set_auto_control", 0)
            board.call("cooling_safety_set_auto_control", 1)
            point(600, state="ABORT")
        elif fault == "OVERTEMP":
            board.ntc1_adc = 400
            point(600, state="ABORT")
            board.ntc1_adc = 2048
        elif fault == "OVERPRESS":
            point(1400, state="ABORT")
        elif fault == "LINK":
            board.tick += 3100
            command("hello", expect=f"@R,H,2,1400,700,{run},HEAT")
            point(600, state="ABORT", ping=False)
        else:
            command("stop", str(run), expect=f"@R,A,{run},STOP")
            assert not board.output, "stop must drop GPIO before next pressure cycle"
            point(600, state="ABORT")
        expected = "SENSOR" if fault in ("NTC0_OPEN", "NTC1_OPEN", "PRESSURE_OPEN", "STALE") else fault
        terminal = events()[-1]
        assert terminal[6] == expected and terminal[11] == "HEAT", (fault, terminal)
        expected_valid = {"NTC0_OPEN": 6, "NTC1_OPEN": 5, "PRESSURE_OPEN": 3, "STALE": 0}
        assert int(terminal[12]) == expected_valid.get(fault, 7), (fault, terminal)
        if fault == "STALE":
            assert int(terminal[4]) == board.tick, "record detection time when ADC snapshot has expired"
        command("start", str(run), expect=f"@R,A,{run},START")
        point(600, state="ABORT")
        ack()
    for command_name, payload, state in (("cfg", cfg + ",1", "HEAT"),
                                         ("mcfg", "900,600,1", "MANUAL")):
        run += 1
        command(command_name, str(run), payload, expect=f"@R,C,{run},{payload}")
        command("start", str(run), expect=f"@R,A,{run},START")
        # Span two PWM windows including the pressure band closest to the target.
        for p in [600, 850, 898] * 7:
            control, _ = point(p, state=state)
            assert control["duty"] == 100 and board.output, (command_name, p, control)
        point(900, state="ABORT" if state=="MANUAL" else "LIMIT")
        if state == "HEAT":
            command("stop", str(run), expect=f"@R,A,{run},STOP")
            point(600, state="ABORT")
        ack()
    print("PASS: relief firmware boot inhibit, real serial configuration and telemetry, "
          "two cycles, event replay, stop, heartbeat, sensors and hard cutoffs")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--mode", choices=("test", "normal"), required=True)
    arguments = parser.parse_args()
    verify_calibration(arguments.elf, arguments.mode == "test")
    if arguments.mode == "normal":
        verify_normal(arguments.elf)
    else:
        verify_relief(arguments.elf)
