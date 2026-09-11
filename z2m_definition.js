import {Zcl} from "zigbee-herdsman";
import {
    presets as e,
    access as ea,
} from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";

const ENDPOINT = 1;
const CLUSTER_NAME = "thermostatSettings";

// Custom server cluster 0xFAAC.
// No manufacturer code was supplied, so this uses normal
// cluster-specific frames without a manufacturer-specific header.
const thermostatSettingsCluster = {
    name: CLUSTER_NAME,
    ID: 0xfaac,
    attributes: {
        controlUsesNtc: {
            name: "controlUsesNtc",
            ID: 0x0000,
            type: Zcl.DataType.BOOLEAN,
            write: true,
            min: 0,
            max: 1,
        },
        hysteresisX100: {
            name: "hysteresisX100",
            ID: 0x0001,
            type: Zcl.DataType.UINT8,
            write: true,
            min: 10,
            max: 200,
        },
        maxValves: {
            name: "maxValves",
            ID: 0x0002,
            type: Zcl.DataType.UINT8,
            write: true,
            min: 1,
            max: 2,
        },
        brightnessPct: {
            name: "brightnessPct",
            ID: 0x0003,
            type: Zcl.DataType.UINT8,
            write: true,
            min: 20,
            max: 100,
        },
        showExtraSensor: {
            name: "showExtraSensor",
            ID: 0x0004,
            type: Zcl.DataType.BOOLEAN,
            write: true,
            min: 0,
            max: 1,
        },
        autoDimEnabled: {
            name: "autoDimEnabled",
            ID: 0x0005,
            type: Zcl.DataType.BOOLEAN,
            write: true,
            min: 0,
            max: 1,
        },
        autoDimTimeoutS: {
            name: "autoDimTimeoutS",
            ID: 0x0006,
            type: Zcl.DataType.UINT16,
            write: true,
            min: 5,
            max: 120,
        },
        dimBrightnessPct: {
            name: "dimBrightnessPct",
            ID: 0x0007,
            type: Zcl.DataType.UINT8,
            write: true,
            min: 5,
            max: 50,
        },
    },
    commands: {
        dimScreen: {
            name: "dimScreen",
            ID: 0x00,
            parameters: [],
        },
    },
    commandsResponse: {},
};

const reportOnChange = {
    min: 0,
    max: "1_HOUR",
    change: 1,
};

const dimScreen = {
    isModernExtend: true,
    exposes: [
        e.enum("dim_screen", ea.SET, ["press"])
            .withLabel("Dim screen")
            .withDescription("Dims the thermostat screen"),
    ],
    toZigbee: [
        {
            key: ["dim_screen"],
            convertSet: async (entity, key, value, meta) => {
                if (value !== "press") {
                    throw new Error(`${key} must be set to 'press'`);
                }

                const endpoint =
                    entity.ID === ENDPOINT
                        ? entity
                        : entity.getEndpoint?.(ENDPOINT);

                if (!endpoint) {
                    throw new Error(`Endpoint ${ENDPOINT} is unavailable`);
                }

                await endpoint.command(
                    CLUSTER_NAME,
                    "dimScreen",
                    {},
                    meta.options ?? {},
                );
            },
        },
    ],
};

export default {
    // Replace these placeholders with the values shown in the
    // Zigbee2MQTT interview log.
    fingerprint: [
        {
            modelID: "Gas Convection Heater",
            manufacturerName: "Peketr",
        },
    ],
    model: "Gas Convection Heater",
    vendor: "Peketr",
    description: "Thermostat with custom thermostat settings",

    extend: [
        // Register the custom cluster before using it.
        m.deviceAddCustomCluster(
            CLUSTER_NAME,
            thermostatSettingsCluster,
        ),

        // Standard HVAC thermostat cluster 0x0201.
        m.thermostat({
            localTemperature: {
                values: {
                    description: "Current thermostat temperature",
                },
            },
            setpoints: {
                values: {
                    occupiedHeatingSetpoint: {
                        min: 7,
                        max: 30,
                        step: 0.5,
                    },
                },
            },
            systemMode: {
                values: ["off", "heat"],
            },
            runningState: {
                values: ["idle", "heat"],
            },
        }),
        // Standard Temperature Measurement cluster 0x0402 on endpoint 1.
        // Reports when temperature changes by 0.1 °C.
        m.temperature({
            reporting: {
                min: "10_SECONDS",
                max: "1_HOUR",
                change: 10,
            },
        }),

        // Standard Relative Humidity Measurement cluster 0x0405 on endpoint 1.
        // Reports when humidity changes by 0.5% RH.
        m.humidity({
            reporting: {
                min: "10_SECONDS",
                max: "1_HOUR",
                change: 50,
            },
        }),

        m.binary({
            name: "control_uses_ntc",
            cluster: CLUSTER_NAME,
            attribute: "controlUsesNtc",
            valueOn: [true, 1],
            valueOff: [false, 0],
            description: "Use the NTC sensor for thermostat control",
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        // The Zigbee value is in hundredths of a degree.
        m.numeric({
            name: "hysteresis",
            cluster: CLUSTER_NAME,
            attribute: "hysteresisX100",
            description: "Thermostat hysteresis",
            unit: "°C",
            valueMin: 0.1,
            valueMax: 2,
            valueStep: 0.1,
            scale: 100,
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.numeric({
            name: "max_valves",
            cluster: CLUSTER_NAME,
            attribute: "maxValves",
            description: "Maximum number of simultaneously open valves",
            valueMin: 1,
            valueMax: 2,
            valueStep: 1,
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.numeric({
            name: "brightness",
            cluster: CLUSTER_NAME,
            attribute: "brightnessPct",
            description: "Screen brightness",
            unit: "%",
            valueMin: 20,
            valueMax: 100,
            valueStep: 10,
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.binary({
            name: "show_extra_sensor",
            cluster: CLUSTER_NAME,
            attribute: "showExtraSensor",
            valueOn: [true, 1],
            valueOff: [false, 0],
            description: "Show the extra sensor on the thermostat screen",
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.binary({
            name: "auto_dim_enabled",
            cluster: CLUSTER_NAME,
            attribute: "autoDimEnabled",
            valueOn: [true, 1],
            valueOff: [false, 0],
            description: "Enable automatic screen dimming",
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.numeric({
            name: "auto_dim_timeout_s",
            cluster: CLUSTER_NAME,
            attribute: "autoDimTimeoutS",
            description: "Automatic screen dimming timeout",
            unit: "s",
            valueMin: 5,
            valueMax: 120,
            valueStep: 5,
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        m.numeric({
            name: "dim_brightness",
            cluster: CLUSTER_NAME,
            attribute: "dimBrightnessPct",
            description: "Brightness while the screen is dimmed",
            unit: "%",
            valueMin: 5,
            valueMax: 50,
            valueStep: 5,
            access: "ALL",
            reporting: reportOnChange,
            entityCategory: "config",
        }),

        dimScreen,
    ],
};