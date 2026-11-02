import * as fz from "zigbee-herdsman-converters/converters/fromZigbee";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";
import * as exposes from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as reporting from "zigbee-herdsman-converters/lib/reporting";
import * as utils from "zigbee-herdsman-converters/lib/utils";

const e = exposes.presets;
const ea = exposes.access;

const attrThermSensorUser = 0xf000;
const attrThermExtTemperatureCalibration = 0xf00c;

const fzLocal = {
    thermostat_programing_oper_mode: {
        cluster: "hvacThermostat",
        type: ["attributeReport", "readResponse"],
        convert: (model, msg, publish, options, meta) => {
            const value = msg.data.programingOperMode;
            if (value === undefined) return;
            return {
                programming_operation_mode: (value & 0x01) > 0 ? "schedule" : "setpoint",
                eco: (value & 0x04) > 0 ? "ON" : "OFF",
            };
        },
    },
};

const tzLocal = {
    thermostat_programing_oper_mode: {
        key: ["programming_operation_mode", "eco"],
        convertSet: async (entity, key, value, meta) => {
            const current = await entity.read("hvacThermostat", ["programingOperMode"]);
            const mode = Number(current.programingOperMode ?? 0);
            if (key === "programming_operation_mode") {
                utils.validateValue(value, ["setpoint", "schedule"]);
                const updated = value === "schedule" ? mode | 0x01 : mode & ~0x01;
                await entity.write("hvacThermostat", {programingOperMode: updated});
                return {state: {programming_operation_mode: value}};
            }
            const ecoOn = value === "ON" || value === "on" || value === true;
            const updated = ecoOn ? mode | 0x04 : mode & ~0x04;
            await entity.write("hvacThermostat", {programingOperMode: updated});
            return {state: {eco: ecoOn ? "ON" : "OFF"}};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read("hvacThermostat", ["programingOperMode"]);
        },
    },
};

const brightnessOnly = ({endpoints, min = 0, max = 8}) => ({
    exposes: endpoints.map(({name, description}) =>
        e.numeric("brightness", ea.ALL).withValueMin(min).withValueMax(max).withDescription(description).withEndpoint(name),
    ),
    fromZigbee: [fz.brightness],
    toZigbee: [{...tz.light_onoff_brightness, endpoints: endpoints.map((c) => c.name)}],
    configure: [
        async (device, coordinatorEndpoint, definition) => {
            const map = definition.endpoint(device);
            for (const {name} of endpoints) {
                const endpoint = device.getEndpoint(map[name]);
                await reporting.bind(endpoint, coordinatorEndpoint, ["genLevelCtrl"]);
                await endpoint.configureReporting("genLevelCtrl", [
                    {attribute: "currentLevel", minimumReportInterval: 5, maximumReportInterval: 3600, reportableChange: 1},
                ]);
                await endpoint.read("genLevelCtrl", ["currentLevel"]);
            }
        },
    ],
    isModernExtend: true,
});

const DAYS = ["sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"];

const scheduleText = (args = {}) => {
    const days = args.days ?? ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"];
    const fetchSchedule = args.fetch ?? false;

    const fromZigbee = [
        {
            cluster: "hvacThermostat",
            type: ["commandSetWeeklySchedule", "commandGetWeeklyScheduleRsp"],
            convert: (model, msg) => {
                const {data} = msg;
                const dayNums = [...Array.from(Array(7).keys()).filter((x) => (2 ** x) & data.dayofweek)];
                const schedule = data.transitions
                    .map((t) => {
                        const temp = t.heatSetpoint ?? t.coolSetpoint;
                        const hh = String(Math.floor(t.transitionTime / 60)).padStart(2, "0");
                        const mm = String(t.transitionTime % 60).padStart(2, "0");
                        return `${hh}:${mm}/${temp / 100.0}°C`;
                    })
                    .join(" ");
                return Object.fromEntries(dayNums.map((d) => [`schedule_${DAYS[d]}`, schedule]));
            },
        },
    ];

    const exposeList = days.map((day) =>
        e.text(`schedule_${day}`, ea.STATE).withDescription(`${day.charAt(0).toUpperCase() + day.slice(1)}'s schedule`),
    );

    const result = {exposes: exposeList, fromZigbee, isModernExtend: true};

    if (fetchSchedule) {
        result.configure = [
            async (device) => {
                const endpoint = device.getEndpoint(1);
                await endpoint.command("hvacThermostat", "getWeeklySchedule", {daystoreturn: 0xff, modetoreturn: 1});
            },
        ];
    }

    return result;
};

export default {
    fingerprint: [
        {modelID: "OS_Thermostat_r01", manufacturerName: "Slacky-DIY"},
        {modelID: "OS_Thermostat_r02", manufacturerName: "Slacky-DIY"},
    ],
    model: "OS_Thermostat_r01",
    vendor: "Slacky-DIY",
    description: "Open Source Thermostat on base Tuya",
    whiteLabel: [{model: "OS_Thermostat_r02", fingerprint: [{modelID: "OS_Thermostat_r02"}]}],
    extend: [
        m.deviceAddCustomCluster("hvacThermostat", {
            name: "hvacThermostat",
            ID: 0x0201,
            attributes: {
                localTemperatureCalibration: {
                    name: "localTemperatureCalibration",
                    ID: 0x0010,
                    type: 0x28,
                    write: true,
                    max: 0xff,
                },
                minSetpointDeadBand: {
                    name: "minSetpointDeadBand",
                    ID: 0x0019,
                    type: 0x28,
                    write: true,
                    max: 0xff,
                },
                sensorUsed: {
                    name: "sensorUsed",
                    ID: attrThermSensorUser,
                    type: 0x30,
                    write: true,
                    max: 0xff,
                },
                extTempCalibration: {
                    name: "extTempCalibration",
                    ID: attrThermExtTemperatureCalibration,
                    type: 0x28,
                    write: true,
                    max: 0xff,
                },
            },
            commands: {},
            commandsResponse: {},
        }),
        m.deviceEndpoints({
            endpoints: {"1": 1, "2": 2},
            multiEndpointSkip: [
                "local_temperature",
                "occupied_heating_setpoint",
                "system_mode",
                "running_state",
                "keypad_lockout",
                "temperature_display_mode",
                "programming_operation_mode",
                "local_temperature_calibration",
                "min_heat_setpoint_limit",
                "max_heat_setpoint_limit",
            ],
        }),
        m.enumLookup({
            name: "keypad_lockout",
            access: "ALL",
            lookup: {unlock: 0, "lock ex on/off": 1, "lock full": 2},
            cluster: "hvacUserInterfaceCfg",
            attribute: "keypadLockout",
            description: "Keypad lockout mode",
        }),
        m.enumLookup({
            name: "sensor_used",
            access: "ALL",
            lookup: {IN: 0, OUT: 1, ALL: 2},
            cluster: "hvacThermostat",
            attribute: "sensorUsed",
            reporting: {min: 0, max: 3600, change: 1},
            description: "Select temperature sensor to use",
        }),
        m.numeric({
            name: "dead_band",
            access: "ALL",
            cluster: "hvacThermostat",
            attribute: "minSetpointDeadBand",
            unit: "°C",
            scale: 10,
            valueMin: 0.5,
            valueMax: 2.5,
            valueStep: 0.1,
            description: "The delta between temperature to trigger activity",
        }),
        m.numeric({
            name: "external_temperature_calibration",
            access: "ALL",
            cluster: "hvacThermostat",
            attribute: "extTempCalibration",
            unit: "°C",
            scale: 10,
            valueMin: -9,
            valueMax: 9,
            valueStep: 0.1,
            description: "Offset to add/subtract to the external temperature",
        }),
        m.numeric({
            name: "temperature",
            access: "STATE_GET",
            cluster: "hvacThermostat",
            attribute: "outdoorTemp",
            reporting: {min: 0, max: 3600, change: 10},
            unit: "°C",
            scale: 100,
            precision: 2,
            description: "Current temperature measured from the floor outer sensor",
        }),
        m.thermostat({
            localTemperature: {},
            localTemperatureCalibration: {values: {min: -9, max: 9, step: 0.1}},
            setpointsLimit: {
                minHeatSetpointLimit: {min: 5, max: 15, step: 0.1},
                maxHeatSetpointLimit: {min: 20, max: 45, step: 0.1},
            },
            runningState: {values: ["idle", "heat"]},
            setpoints: {values: {occupiedHeatingSetpoint: {min: 5, max: 45, step: 0.1}}},
            systemMode: {values: ["off", "heat"]},
            programmingOperationMode: {values: ["setpoint", "schedule"], toZigbee: {skip: true}},
            weeklySchedule: {values: ["heat"]},
        }),
        {
            exposes: [
                e
                    .binary("eco", ea.ALL, "ON", "OFF")
                    .withDescription("Eco mode (programingOperMode bit 0x04), works in both setpoint and schedule modes"),
            ],
            fromZigbee: [fzLocal.thermostat_programing_oper_mode],
            toZigbee: [tzLocal.thermostat_programing_oper_mode],
            configure: [
                async (device) => {
                    const endpoint = device.getEndpoint(1);
                    await endpoint.configureReporting("hvacThermostat", [
                        {attribute: "programingOperMode", minimumReportInterval: 0, maximumReportInterval: 3600, reportableChange: 0},
                    ]);
                },
            ],
            isModernExtend: true,
        },
        scheduleText({fetch: true}),
        brightnessOnly({
            endpoints: [
                {name: "1", description: "Brightness level 6:00-22:00 (day)"},
                {name: "2", description: "Brightness level 22:00-6:00 (night)"},
            ],
            min: 0,
            max: 8,
        }),
    ],
    meta: {},
    ota: true,
};
