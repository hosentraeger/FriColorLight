import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';

const e = exposes.presets;

/* ========= WALLSWITCH (unverändert) ========= */
const wallswitch = {
    zigbeeModel: ['4-gang-wallswitch'],
    model: 'Four Gang Wallswitch (TI)',
    vendor: 'redfivedesigns',
    description: 'Custom 4-gang switch',
    extend: [
        m.deviceEndpoints({"endpoints": {"1": 1, "2": 2, "3": 3, "4": 4}}),
        m.commandsOnOff({"endpointNames": ["1", "2", "3", "4"]}),
    ],
    fromZigbee: [{
        cluster: 'genBasic',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            if (msg.data.physicalEnvironment !== undefined) {
                const modes = {0: 'brightness', 1: 'color_temp', 2: 'hue'};
                result.control_mode = modes[msg.data.physicalEnvironment];
            }
            if (msg.data.locationDescription !== undefined) {
                const actions = {2: 'double', 4: 'hold', 5: 'release'};
                result.action = actions[msg.data.locationDescription];
            }
            return result;
        },
    }],
    exposes: [
        e.enum('control_mode', 1, ['brightness', 'color_temp', 'hue']),
        e.enum('action', 1, ['single', 'double', 'hold', 'release', 'toggle_1']),
    ],
};

/* ========= LAMPE ========= */
const colorlight = {
    zigbeeModel: ['rgbww-colorlight'],
    model: 'RGBWW Color Light',
    vendor: 'redfivedesigns',
    description: 'ESP Zigbee RGBWW light (Debug Version)',
    extend: [m.light({"color":true}), m.commandsOnOff(), m.commandsLevelCtrl(), m.commandsColorCtrl()],
    meta: {
        otaMaximumDataSize: 220
    },
    ota: true,
};

export default [wallswitch, colorlight];
