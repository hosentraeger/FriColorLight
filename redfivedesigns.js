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

    endpoint: (device) => ({rgb1: 1, rgb2: 2, tw: 3}),

    fromZigbee: [{
        cluster: 'genOnOff',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('onOff')) {
                const names = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
                const name = names[msg.endpoint.ID];
                if (name) return {[`state_${name}`]: msg.data['onOff'] === 1 ? 'ON' : 'OFF'};
            }
        },
    }, {
        cluster: 'genLevelCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('currentLevel')) {
                const names = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
                const name = names[msg.endpoint.ID];
                if (name) return {[`brightness_${name}`]: msg.data['currentLevel']};
            }
        },
    }, {
        cluster: 'lightingColorCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const names = {1: 'rgb1', 2: 'rgb2', 3: 'tw'};
            const name = names[msg.endpoint.ID];
            if (!name) return;
            const result = {};
            if (msg.data.hasOwnProperty('currentX') && msg.data.hasOwnProperty('currentY')) {
                result[`color_${name}`] = {
                    x: msg.data['currentX'] / 65535,
                    y: msg.data['currentY'] / 65535,
                };
            }
            if (msg.data.hasOwnProperty('colorTemperature')) {
                result[`color_temp_${name}`] = msg.data['colorTemperature'];
            }
            return result;
        },
    }],

    toZigbee: [{
        key: ['state', 'state_rgb1', 'state_rgb2', 'state_tw'],
        convertSet: async (entity, key, value, meta) => {
            const epMap = {state: 1, state_rgb1: 1, state_rgb2: 2, state_tw: 3};
            const ep = meta.device.getEndpoint(epMap[key]);
            await ep.command('genOnOff', value === 'ON' ? 'on' : 'off', {});
            return {state: {[key]: value}};
        },
    }, {
        key: ['brightness', 'brightness_rgb1', 'brightness_rgb2', 'brightness_tw'],
        convertSet: async (entity, key, value, meta) => {
            const epMap = {brightness: 1, brightness_rgb1: 1, brightness_rgb2: 2, brightness_tw: 3};
            const ep = meta.device.getEndpoint(epMap[key]);
            await ep.command('genLevelCtrl', 'moveToLevelWithOnOff', {level: value, transtime: 0});
            return {state: {[key]: value}};
        },
    }, {
        key: ['color', 'color_rgb1', 'color_rgb2'],
        convertSet: async (entity, key, value, meta) => {
            const epMap = {color: 1, color_rgb1: 1, color_rgb2: 2};
            const ep = meta.device.getEndpoint(epMap[key]);
            await ep.command('lightingColorCtrl', 'moveToColor', {
                colorx: Math.round(value.x * 65535),
                colory: Math.round(value.y * 65535),
                transtime: 0,
            });
            return {state: {[key]: value}};
        },
    }, {
        key: ['color_temp', 'color_temp_tw'],
        convertSet: async (entity, key, value, meta) => {
            const ep = meta.device.getEndpoint(3);
            await ep.command('lightingColorCtrl', 'moveToColorTemp', {
                colortemp: value,
                transtime: 0,
            });
            return {state: {[key]: value}};
        },
    }],
    exposes: [
        e.light().withBrightness().withColor(['xy']).withEndpoint('rgb1'),
        e.light().withBrightness().withColor(['xy']).withEndpoint('rgb2'),
        e.light().withBrightness().withColorTemp(153, 500).withEndpoint('tw'),
    ],
};

export default [wallswitch, colorlight];
