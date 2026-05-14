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
    description: 'ESP Zigbee RGBWW light',

    endpoint: (device) => {
        return {
            rgb1: 1,
            rgb2: 2,
            tw:   3,
        };
    },

    fromZigbee: [{
        cluster: 'genOnOff',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('onOff')) {
                const endpointNames = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
                const name = endpointNames[msg.endpoint.ID];
                if (name) return {[`state_${name}`]: msg.data['onOff'] === 1 ? 'ON' : 'OFF'};
            }
        },
    },
    {
        cluster: 'genLevelCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('currentLevel')) {
                const endpointNames = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
                const name = endpointNames[msg.endpoint.ID];
                if (name) return {[`brightness_${name}`]: msg.data['currentLevel']};
            }
        },
    },
    {
        cluster: 'lightingColorCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const endpointNames = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
            const name = endpointNames[msg.endpoint.ID];
            if (!name) return;

            const result = {};
            if (msg.data.hasOwnProperty('currentX') && msg.data.hasOwnProperty('currentY')) {
                result[`color_${name}`] = {x: msg.data['currentX'] / 65535, y: msg.data['currentY'] / 65535};
            }
            if (msg.data.hasOwnProperty('colorTemperature')) {
                result[`color_temp_${name}`] = msg.data['colorTemperature'];
            }
            return result;
        },
    }],

    extend: [
        m.light({
            endpointNames: ['rgb1'],
            color: {xy: true},
            brightness: true,
        }),
        m.light({
            endpointNames: ['rgb2'],
            color: {xy: true},
            brightness: true,
        }),
        m.light({
            endpointNames: ['tw'],
            colorTemp: {range: [153, 500]},
            brightness: true,
        }),
    ],
};

export default [wallswitch, colorlight];