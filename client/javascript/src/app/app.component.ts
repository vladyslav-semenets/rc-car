import { Component, OnInit } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { AppService } from "./app.service";
import { FormsModule } from '@angular/forms';
import { MatCard, MatCardContent } from '@angular/material/card';
import { NgIf } from '@angular/common';
import { MatProgressSpinner } from '@angular/material/progress-spinner';
import { distinctUntilChanged, filter, fromEvent, map } from 'rxjs';

interface IControllerEvent extends Event {
	detail: {
		value: number;
		name: string;
		position: {
			x: number;
			y: number;
		};
		controller?: {
			index: number;
			name: string;
		};
	};
}

@Component({
    selector: 'app-root',
    imports: [
        RouterOutlet,
        FormsModule,
        MatCard,
        MatCardContent,
        NgIf,
        MatProgressSpinner,
    ],
    templateUrl: './app.component.html',
    styleUrl: './app.component.scss'
})
export class AppComponent implements OnInit {
	direction: unknown;
	controller: unknown = null;
	turnTo: unknown;
	isCarStarted: boolean = false;
	degreeOfTurns: number = 87;
	pwmFrequency: number = 25000;
	carSpeed: number = 50;
	isCameraStarted: boolean = true;
	rcCarCameraUrl: string = '';

	constructor(
		private _appService: AppService,
	) {
		this._appService.initWebSocket();
	}

	public ngOnInit(): void {
		this.setUpController();

		this._appService.socket.addEventListener(
			'message',
			(event) => {
				try {
					const payload = JSON.parse(event.data);

					this.isCarStarted = !!payload?.carStarted;

					if (payload?.cameraURL) {
						this.rcCarCameraUrl = `rtsp://${payload.cameraURL.replace('tcp://', '')}/cam1`;
					}

				} catch (e) {
					console.error(e);
				}
			});

		this._appService.socket.addEventListener(
			'open',
			() => {
				this.sendSocketMessage('init', {
					carSpeed: this.carSpeed,
					degreeOfTurns: this.degreeOfTurns,
				});
			},
		);
	}

	public stopCar(): void {
		this.direction = null;
		this.sendSocketMessage('stop-car', {});
	}

	public startCamera(): void {
		this.sendSocketMessage('start-camera', {});
	}

	public stopCamera(): void {
		this.sendSocketMessage('stop-camera', {});
	}

	public setEscToNeutralPosition(): void {
		this.direction = null;
		this.sendSocketMessage('set-esc-to-neutral-position', {});
	}

	public breakingCar(): void {
		this.sendSocketMessage('breaking', {});
	}

	public resetTurns(): void {
		this.turnTo = null;
		this.sendSocketMessage('reset-turns', {
			degreeOfTurns: this.degreeOfTurns,
		});
	}

	public resetCameraGimbal(): void {
		this.sendSocketMessage('reset-camera-gimbal', {});
	}

	public onChangeDegreeOfTurns(value: number): void {
		this.sendSocketMessage('change-degree-of-turns', {
			degreeOfTurns: value,
		});
	}

	private setUpController(): void {
		// @ts-ignore
		window.Controller.search();

		fromEvent<IControllerEvent>(window, 'gc.controller.found').subscribe(
			(event) => {
				const controller = event.detail.controller;
				console.log("Controller found at index " + controller?.index + ".");
				console.log("'" + controller?.name + "' is ready!");
				this.controller = controller;
			},
		);

		fromEvent<IControllerEvent>(window, 'gc.button.press').subscribe(
			(event: IControllerEvent) => {
				if (event.detail.name === 'FACE_1') {
					this.stopCar();
				}

				if (event.detail.name === 'FACE_2') {
					this.startCamera();
				}

				if (event.detail.name === 'FACE_3') {
					this.stopCamera();
				}

				if (event.detail.name === 'FACE_4') {
					this.setEscToNeutralPosition();
				}

				if (event.detail.name === 'DPAD_RIGHT') {
					this.degreeOfTurns--;
					this.onChangeDegreeOfTurns(this.degreeOfTurns);
				}

				if (event.detail.name === 'DPAD_LEFT') {
					this.degreeOfTurns++;
					this.onChangeDegreeOfTurns(this.degreeOfTurns);
				}

				if (event.detail.name === 'LEFT_SHOULDER') {
					this.breakingCar();
				}
			},
		);

		fromEvent<IControllerEvent>(window, 'gc.button.release').pipe(
			filter(
				(event: IControllerEvent) => [
					'RIGHT_SHOULDER_BOTTOM',
					'LEFT_SHOULDER_BOTTOM',
				].includes(event.detail.name),
			),
		).subscribe(() => {
			this.setEscToNeutralPosition();
		});

		// turn gimbal top
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'RIGHT_ANALOG_STICK' && event.detail.position.y < 0
			),
			map(
				(event: IControllerEvent) => this.mapStickToAngle(
					event.detail.position.y,
					0,
					90,
					0.01,
				)),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.sendSocketMessage('camera-gimbal-set-pitch-angle', {
				degrees,
			});
		});

		// turn gimbal bottom
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'RIGHT_ANALOG_STICK' && Math.sign(event.detail.position.y) !== -1 && event.detail.position.y > 0
			),
			map(
				(event: IControllerEvent) => {
					let value  = event.detail.position.y;

					if (value < 0.01) {
						value = 0.01;
					}
					if (value > 1) {
						value = 1;
					}

					return value * -79;
				}),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.sendSocketMessage('camera-gimbal-set-pitch-angle', {
				degrees,
			});
		});

		// turn gimbal to right
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'RIGHT_ANALOG_STICK' && Math.sign(event.detail.position.x) !== -1
			),
			map(
				(event: IControllerEvent) => this.mapStickToAngle(
					event.detail.position.x,
					90,
					0,
					0.01,
				)),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.sendSocketMessage('camera-gimbal-turn-to', {
				degrees,
			});
		});

		// turn gimbal to left
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'RIGHT_ANALOG_STICK' && event.detail.position.x < 0
			),
			map(
				(event: IControllerEvent) => Math.abs(this.mapStickToAngle(
					event.detail.position.x,
					0,
					90,
					0.01,
				)) * -1),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.sendSocketMessage('camera-gimbal-turn-to', {
				degrees,
			});
		});

		// turn to right
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'LEFT_ANALOG_STICK' && Math.sign(event.detail.position.x) !== -1
			),
			map(
				(event: IControllerEvent) => this.mapStickToAngle(
					event.detail.position.x,
					0,
					this.degreeOfTurns,
					0.01,
				)),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.turnTo = 'right';
			this.sendSocketMessage('turn-to', {
				degrees,
			});
		});

		// turn to left
		fromEvent<IControllerEvent>(window, 'gc.analog.hold').pipe(
			filter(
				(event: IControllerEvent) =>
					event.detail.name === 'LEFT_ANALOG_STICK' && event.detail.position.x < 0
			),
			map(
				(event: IControllerEvent) => this.mapStickToAngle(
					event.detail.position.x,
					this.degreeOfTurns,
					140,
					0.01,
				)
			),
			distinctUntilChanged(),
		).subscribe((degrees: number) => {
			this.turnTo = 'left';
			this.sendSocketMessage('turn-to', {
				degrees,
			});
		});

		fromEvent<IControllerEvent>(window, 'gc.analog.end').subscribe(
			(event: IControllerEvent) => {
				if (event.detail.name === 'LEFT_ANALOG_STICK') {
					this.resetTurns();
				}
			},
		);

		fromEvent<IControllerEvent>(window, 'gc.analog.end').subscribe(
			(event: IControllerEvent) => {
				if (event.detail.name === 'RIGHT_ANALOG_STICK') {
					this.resetCameraGimbal();
				}
			},
		);

		fromEvent<IControllerEvent>(window, 'gc.button.hold').pipe(
			filter((event: IControllerEvent) => event.detail.name === 'RIGHT_SHOULDER_BOTTOM'),
			map((event: IControllerEvent) => this.buttonValueToCarSpeed(event.detail.value)),
			distinctUntilChanged(),
		).subscribe((carSpeed: number) => {
			this.sendSocketMessage('forward', {
				pwmFrequency: this.pwmFrequency,
				carSpeed,
			});
		});

		fromEvent<IControllerEvent>(window, 'gc.button.hold').pipe(
			filter((event: IControllerEvent) => event.detail.name === 'LEFT_SHOULDER_BOTTOM'),
			map((event: IControllerEvent) => this.buttonValueToCarSpeed(event.detail.value)),
			distinctUntilChanged(),
		).subscribe((carSpeed: number) => {
			this.sendSocketMessage('backward', {
				pwmFrequency: this.pwmFrequency,
				carSpeed,
			});
		});
	}

	private buttonValueToCarSpeed(rawValue: number, maxSpeed: number = 100): number {
		if (rawValue < 0) {
			rawValue = 0;
		}

		let speed = Math.round((rawValue / 0.6) * 100);

		return speed > maxSpeed ? maxSpeed : speed;
	}

	private mapStickToAngle(
		stickValue: number,
		angleMin: number,
		angleMax: number,
		step: number
	): number {
		if (Math.sign(stickValue) !== -1) {
			let angle = angleMax - stickValue * (angleMax - angleMin);
			angle = Math.round(angle / step) * step;
			return Math.min(Math.max(angle, Math.min(angleMin, angleMax)), Math.max(angleMin, angleMax));
		}

		const clampedValue = Math.max(Math.min(stickValue, 0), -1);
		const angle = angleMax + (clampedValue + 1) * (angleMin - angleMax);
		const roundedAngle = Math.round(angle / step) * step;

		return Math.max(angleMin, Math.min(roundedAngle, angleMax));
	}

	private sendSocketMessage(action: string, data: Record<string, any> = {}): void {
		this._appService.socket.send(
			JSON.stringify({ to: 'rc-car-server', data: { action, ...data } })
		);
	}
}
