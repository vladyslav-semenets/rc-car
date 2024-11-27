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
        this.setPwmPulse(this.gmPwm1, this.maxPulseWidth);
    }

    setPwmPulse(pin, pulseWidth) {
        pin.servoWrite(pulseWidth);
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
    isEngineRunning = false;
    constructor() {
        this.servo = new Gpio(17, { mode: Gpio.OUTPUT });
        this.escPin = new Gpio(23, { mode: Gpio.OUTPUT });

        this.escMinPulseWidth = 1000;
        this.escMaxPulseWidth = 2000;
        this.escNeutralPulseWidth = 1500;

        this.carSpeed = 50;
        this.turnsDegrees = 87;
        this.minPulseWidth = 500;
        this.maxPulseWidth = 2500;
    }

    speed2pwm(speed, direction) {
        speed = Math.max(1, Math.min(speed, 100));

        let pulseWidth;

        if (direction === 'forward') {
            pulseWidth = this.escNeutralPulseWidth + (speed / 100) * (this.escMaxPulseWidth - this.escNeutralPulseWidth);
        } else if (direction === 'backward') {
            pulseWidth = this.escNeutralPulseWidth - (speed / 100) * (this.escNeutralPulseWidth - this.escMinPulseWidth);
        } else {
            pulseWidth = this.escNeutralPulseWidth;
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

    stopCar() {
        setTimeout(() => {
            this.setEscSpeed(this.escNeutralPulseWidth);

            setTimeout(() => {
                this.setEscSpeed(this.speed2pwm(50, 'backward'));
            }, 200);

            this.isEngineRunning = false;
        }, 200);
    }

    breaking() {
        this.setEscSpeed(this.escNeutralPulseWidth);
        setTimeout(() => {
            this.setEscSpeed(this.escNeutralPulseWidth);
        }, 100);
        this.isEngineRunning = false;
    }

    setEscToNeutralPosition() {
        this.setEscSpeed(this.escNeutralPulseWidth);
    }

    moveCar(direction, speed) {
        const pwmValue = this.speed2pwm(speed, direction);
        this.setEscSpeed(pwmValue);

        this.isEngineRunning = true;
    }

    turn(degrees) {
        this.turnsDegrees = degrees;
        this.setServoAngle(degrees);
    }
}

class MediamtxController {
    constructor() {
        this.mediamtxProcess = null;
    }

    run() {
        if (this.mediamtxProcess) {
            return;
        }

        this.mediamtxProcess = spawn('mediamtx', [], { stdio: ['inherit', 'inherit'] });
    }

    stop() {
        this.mediamtxProcess?.kill('SIGINT');
        this.mediamtxProcess = null;
    }
}

class SocketController {
    constructor(carController, mediamtxController, cameraGimbal) {
        this.carController = carController;
        this.mediamtxController = mediamtxController;
        this.cameraGimbal = cameraGimbal;
        this.wss = new WebSocket(
            'ws://127.0.0.1:8585',
            {
                headers: {
                    'X-Source': 'rc-car-server',
                },
            },
        );
        this.initSocket();
    }

    initSocket() {
        this.wss.on('error', console.error);

        this.wss.on('open', () => {
            this.wss.send(JSON.stringify({ to: 'rc-car-client', data: { status: 'started' } }));

            console.log('Connected to Socket Server');
        });

        this.wss.on('close', () => {
            this.carController?.stopCar();
            this.mediamtxController?.stop();

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
                        this.carController.turn(parseInt(options?.degreeOfTurns ?? this.turnsDegrees, 10));
                        this.carController.setEscToNeutralPosition();
                        this.carController.isEngineRunning = false;
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
                        this.mediamtxController.run();
                        break;

                    case 'stop-camera':
                        this.mediamtxController.stop();
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
                        this.carController.moveCar(action, options?.carSpeed);
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
const mediamtxController = new MediamtxController();
const socketController = new SocketController(carController, mediamtxController, cameraGimbal);

process.on('SIGINT', () => {
    socketController.closeSocket();
    carController?.stopCar();
    mediamtxController?.stop();

    terminate();
});
