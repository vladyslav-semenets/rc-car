import { WebSocket } from 'ws';
import { Gpio, terminate } from 'pigpio';
import { spawn } from 'child_process';

class CameraGimbal {
    constructor() {
        this.minPulseWidth = 1000;
        this.maxPulseWidth = 2000;
        this.gmPwm1 = new Gpio(27, { mode: Gpio.OUTPUT });
        this.gmPwm3 = new Gpio(22, { mode: Gpio.OUTPUT });
        this.gmPwm4 = new Gpio(24, { mode: Gpio.OUTPUT });
    }

    init() {
        this.gmPwm1.servoWrite(this.maxPulseWidth);
    }

    angleToPulseWidth(angle) {
        if (angle < -90) {
            angle = -90;
        }

        if (angle > 90) {
            angle = 90;
        }

        const pulseWidth = ((angle + 90) / 180) * (this.maxPulseWidth - this.minPulseWidth) + this.minPulseWidth;
        return Math.round(pulseWidth);
    }

    setYawAngle(angle) {
        const pulseWidth = this.angleToPulseWidth(angle);
        this.gmPwm4.servoWrite(pulseWidth);
    }

    setPitchAngle(angle) {
        const pulseWidth = this.angleToPulseWidth(angle);
        this.gmPwm3.servoWrite(pulseWidth);
    }
}

class CarController {
    constructor() {
        this.servo = new Gpio(17, { mode: Gpio.OUTPUT });
        this.escPin = new Gpio(23, { mode: Gpio.OUTPUT });

        this.escMinPulseWidth = 1000;
        this.escMaxPulseWidth = 2000;
        this.escNeutralPulseWidth = 1500;

        this.carSpeed = 50;
        this.minPulseWidth = 500;
        this.maxPulseWidth = 2500;
    }

    speedToPulseWidth(speed, direction) {
        speed = Math.max(1, Math.min(speed, 100));

        let pulseWidth = this.escNeutralPulseWidth;

        if (direction === 'forward') {
            pulseWidth = this.escNeutralPulseWidth + (speed / 100) * (this.escMaxPulseWidth - this.escNeutralPulseWidth);
        } else if (direction === 'backward') {
            pulseWidth = this.escNeutralPulseWidth - (speed / 100) * (this.escNeutralPulseWidth - this.escMinPulseWidth);
        }

        return pulseWidth;
    }

    setEscSpeed(pulseWidth) {
        this.escPin.servoWrite(pulseWidth);
    }

    setServoAngle(angle) {
        const pulseWidth = Math.floor(this.minPulseWidth + (angle / 180.0) * (this.maxPulseWidth - this.minPulseWidth));
        this.servo.servoWrite(pulseWidth);
    }

    stop() {
        setTimeout(() => {
            this.setEscSpeed(this.escNeutralPulseWidth);

            setTimeout(() => {
                this.setEscSpeed(this.speedToPulseWidth(50, 'backward'));
            }, 200);
        }, 200);
    }

    breaking() {
        this.setEscSpeed(this.escNeutralPulseWidth);
        setTimeout(() => {
            this.setEscSpeed(this.escNeutralPulseWidth);
        }, 100);
    }

    setEscToNeutralPosition() {
        this.setEscSpeed(this.escNeutralPulseWidth);
    }

    move(direction, speed) {
        const pwmValue = this.speedToPulseWidth(speed, direction);
        this.setEscSpeed(pwmValue);
    }

    turn(degrees) {
        this.setServoAngle(degrees);
    }
}

class MediaMtxController {
    constructor() {
        this.mediamtxProcess = null;
    }

    run() {
        if (this.mediamtxProcess) {
            return;
        }

        this.mediamtxProcess = spawn(
            process.env.MEDIAMTX_BIN_PATH,
            [process.env.MEDIAMTX_CONFIG_PATH],
            {
                stdio: ['inherit', 'inherit'],
            },
        );
    }

    stop() {
        this.mediamtxProcess?.kill('SIGINT');
        this.mediamtxProcess = null;
    }
}

class SocketController {
    constructor(carController, mediaMtxController, cameraGimbal) {
        this.carController = carController;
        this.mediaMtxController = mediaMtxController;
        this.cameraGimbal = cameraGimbal;
        this.wss = new WebSocket(`ws://${process.env.RASPBERRY_PI_IP}:8585/?source=rc-car-server`);
        this.initSocket();
    }

    initSocket() {
        this.wss.on('error', console.error);

        this.wss.on('open', () => {
            this.wss.send(JSON.stringify({ to: 'rc-car-client', data: { status: 'started' } }));

            console.log('Connected to Socket Server');
        });

        this.wss.on('close', () => {
            this.carController?.stop();
            this.mediaMtxController?.stop();

            this.wss.send(JSON.stringify({ to: 'rc-car-client', data: { status: 'stopped' } }));

            console.log('Disconnected from Socket Server');
        });

        this.wss.on('message', (rawData) => this.handleMessage(rawData));
    }

    closeSocket() {
        this.wss.close();
    }

    handleMessage(rawData) {
        try {
            const { data: { action, ...options } = { } } = JSON.parse(rawData.toString());

            if (action) {
                switch (action) {
                    case 'init':
                        this.carController.carSpeed = Math.min(parseInt(options?.carSpeed, 10), 100);
                        this.carController.turn(parseInt(options?.degreeOfTurns, 10));
                        this.carController.setEscToNeutralPosition();
                        break;

                    case 'stop-car':
                        this.carController.stopCar();
                        break;

                    case 'breaking':
                        this.carController.breaking();
                        break;

                    case 'reset-turns':
                        this.carController.turn(options?.degreeOfTurns ?? 90);
                        break;

                    case 'start-camera':
                        this.mediaMtxController.run();
                        break;

                    case 'stop-camera':
                        this.mediaMtxController.stop();
                        break;

                    case 'camera-gimbal-turn-to':
                        this.cameraGimbal.setYawAngle(options?.degrees);
                        break;

                    case 'camera-gimbal-set-pitch-angle':
                        this.cameraGimbal.setPitchAngle(options?.degrees);
                        break;

                    case 'reset-camera-gimbal':
                        this.cameraGimbal.setYawAngle(0);
                        this.cameraGimbal.setPitchAngle(0);
                        break;

                    case 'change-degree-of-turns':
                        this.carController.turn(parseInt(options?.degreeOfTurns, 10));
                        break;

                    case 'backward':
                    case 'forward':
                        this.carController.move(action, options?.carSpeed);
                        break;

                    case 'set-esc-to-neutral-position':
                        this.carController.setEscToNeutralPosition()
                        break;

                    case 'turn-to':
                        this.carController.turn(parseInt(options?.degrees, 10));
                        break;
                }
            }
        } catch (error) {
            console.error('Error handling message:', error);
        }
    }
}

const cameraGimbal = new CameraGimbal();
const carController = new CarController();
const mediaMtxController = new MediaMtxController();
const socketController = new SocketController(carController, mediaMtxController, cameraGimbal);

process.on('SIGINT', () => {
    socketController.closeSocket();
    carController?.stop();
    mediaMtxController?.stop();
    terminate();
});
