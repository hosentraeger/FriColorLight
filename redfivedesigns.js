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

/* ========= LAMPE – 3 Endpoints ========= */
const colorlight = {
    zigbeeModel: ['rgbww-colorlight'],
    model: 'RGBWW Color Light',
    vendor: 'redfivedesigns',
    description: 'ESP Zigbee RGBWW light (RGB PWM + RGB SK6812 + Tunable White)',
    extend: [
        m.light({
            endpointNames: ['ep1_rgb_pwm'],
            color: { modes: ['xy', 'hs'], enhancedHue: true },
        }),
        m.light({
            endpointNames: ['ep2_rgb_sk6812'],
            color: { modes: ['xy', 'hs'], enhancedHue: true },
        }),
        m.light({
            endpointNames: ['ep3_tunable_white'],
            colorTemp: { range: [153, 500] },
        }),
        m.commandsOnOff(),
        m.commandsLevelCtrl(),
        m.commandsColorCtrl(),
    ],
    endpoint: (device) => ({
        ep1_rgb_pwm:       1,
        ep2_rgb_sk6812:    2,
        ep3_tunable_white: 3,
    }),
    configure: async (device, coordinatorEndpoint) => {
        for (const epNum of [1, 2]) {
            const ep = device.getEndpoint(epNum);
            await ep.bind('genOnOff',          coordinatorEndpoint);
            await ep.bind('genLevelCtrl',      coordinatorEndpoint);
            await ep.bind('lightingColorCtrl', coordinatorEndpoint);

            await ep.configureReporting('genOnOff', [
                { attribute: 'onOff',        min: 0, max: 600, change: 1 },
            ]);
            await ep.configureReporting('genLevelCtrl', [
                { attribute: 'currentLevel', min: 0, max: 600, change: 1 },
            ]);
            // Nur currentX/currentY – der Stack reportet diese nach moveToColor
            await ep.configureReporting('lightingColorCtrl', [
                { attribute: 'currentX',     min: 0, max: 600, change: 1 },
                { attribute: 'currentY',     min: 0, max: 600, change: 1 },
            ]);
        }

        const ep3 = device.getEndpoint(3);
        await ep3.bind('genOnOff',          coordinatorEndpoint);
        await ep3.bind('genLevelCtrl',      coordinatorEndpoint);
        await ep3.bind('lightingColorCtrl', coordinatorEndpoint);

        await ep3.configureReporting('genOnOff', [
            { attribute: 'onOff',            min: 0, max: 600, change: 1 },
        ]);
        await ep3.configureReporting('genLevelCtrl', [
            { attribute: 'currentLevel',     min: 0, max: 600, change: 1 },
        ]);
        await ep3.configureReporting('lightingColorCtrl', [
            { attribute: 'colorTemperature', min: 0, max: 600, change: 1 },
        ]);
    },
    meta: {
        multiEndpoint: true,
        otaMaximumDataSize: 220,
    },
    ota: true,
};

export default [wallswitch, colorlight];
