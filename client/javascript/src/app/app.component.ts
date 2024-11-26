import { Component, OnInit } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { MatButton } from "@angular/material/button";
import { MatIcon } from "@angular/material/icon";
import { AppService } from "./app.service";
import { MatSlider, MatSliderThumb } from '@angular/material/slider';
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
	standalone: true,
	imports: [
		RouterOutlet,
		MatButton,
		MatIcon,
		MatSlider,
		MatSliderThumb,
		FormsModule,
		MatCard,
		MatCardContent,
		NgIf,
		MatProgressSpinner,
	],
	templateUrl: './app.component.html',
	styleUrls: ['./app.component.scss']
})
export class AppComponent implements OnInit {
	direction: unknown;
	controller: IControllerEvent['detail']['controller'];
	turnTo: unknown;
	isCarStarted = false;
	degreeOfTurns = 87;
	pwmFrequency = 25000;
	carSpeed = 50;
	isCameraStarted = true;
	rcCarCameraUrl = '';

	constructor(private appService: AppService) {
		this.appService.initWebSocket();
	}

	ngOnInit(): void {
		this.setupController();
		this.setupSocketListeners();
	}

	private setupSocketListeners(): void {
		this.appService.socket.addEventListener('message', this.handleSocketMessage.bind(this));
		this.appService.socket.addEventListener('open', this.initializeCar.bind(this));
	}

	private handleSocketMessage(event: MessageEvent): void {
		try {
			const payload = JSON.parse(event.data);

			this.isCarStarted = !!payload?.carStarted;
			if (payload?.cameraURL) {
				this.rcCarCameraUrl = `rtsp://${payload.cameraURL.replace('tcp://', '')}/cam1`;
			}
		} catch (error) {
			console.error('Error parsing socket message:', error);
		}
	}

	private initializeCar(): void {
		this.appService.socket.send(JSON.stringify({
			to: 'rc-car-server',
			data: {
				action: 'init',
				carSpeed: this.carSpeed,
				degreeOfTurns: this.degreeOfTurns,
			},
		}));
	}

	private setupController(): void {
		(window as any)?.Controller?.search();

		fromEvent<IControllerEvent>(window, 'gc.controller.found')
			.subscribe(
				{
					next: (event) => {
						this.controller = event.detail.controller;
						console.log(`Controller found: ${this.controller?.name} (Index: ${this.controller?.index})`);
					},
				},
			);

		this.setupControllerButtons();
		this.setupAnalogStickEvents();
	}

	private setupControllerButtons(): void {
		const buttonActions: Record<string, () => void> = {
			'FACE_1': () => this.stopCar(),
			'FACE_2': () => this.startCamera(),
			'FACE_3': () => this.stopCamera(),
			'FACE_4': () => this.setEscToNeutralPosition(),
			'DPAD_RIGHT': () => this.updateDegreeOfTurns(-1),
			'DPAD_LEFT': () => this.updateDegreeOfTurns(1),
			'LEFT_SHOULDER': () => this.breakCar(),
		};

		fromEvent<IControllerEvent>(window, 'gc.button.press')
			.subscribe(
				{
					next: (event) => {
						const action = buttonActions[event.detail.name];
						if (action) {
							action();
						}
					}
				}
			);

		fromEvent<IControllerEvent>(window, 'gc.button.release')
			.pipe(
				filter(
					event => [
						'RIGHT_SHOULDER_BOTTOM',
						'LEFT_SHOULDER_BOTTOM',
					].includes(event.detail.name),
				),
			)
			.subscribe(
				{
					next: () => this.setEscToNeutralPosition(),
				},
			);
	}

	private setupAnalogStickEvents(): void {
		this.configureAnalogStick(
			'RIGHT_ANALOG_STICK',
			'camera-gimbal-set-pitch-angle',
			{
				y: [0, 90, 0.01],
				negate: true,
			},
		);
		this.configureAnalogStick(
			'RIGHT_ANALOG_STICK',
			'camera-gimbal-set-pitch-angle',
			{
				y: [0, -79, 0.01],
			},
		);
		this.configureAnalogStick(
			'LEFT_ANALOG_STICK',
			'turn-to',
			{
				x: [0, this.degreeOfTurns, 0.01],
				action: 'right',
			},
		);
		this.configureAnalogStick(
			'LEFT_ANALOG_STICK',
			'turn-to',
			{
				x: [this.degreeOfTurns, 140, 0.01],
				action: 'left',
			},
		);
	}

	private configureAnalogStick(
		stickName: string,
		action: string,
		{
			x,
			y,
			negate,
			action: turnDirection,
		}: {
			x?: [number, number, number];
			y?: [number, number, number];
			negate?: boolean;
			action?: string;
		}
	): void {
		fromEvent<IControllerEvent>(window, 'gc.analog.hold')
			.pipe(
				filter(event => event.detail.name === stickName && this.shouldTriggerAnalogEvent(event, x, y, negate)),
				map(event => this.mapStickToAngle(event.detail.position, x, y)),
				distinctUntilChanged(),
			)
			.subscribe((degrees: number) => {
				this.turnTo = turnDirection || null;
				this.sendAction(action, { degrees });
			});
	}

	private shouldTriggerAnalogEvent(
		event: IControllerEvent,
		x?: [number, number, number],
		y?: [number, number, number],
		negate?: boolean,
	): boolean {
		if (x && Math.sign(event.detail.position.x) !== -1 !== negate) {
			return true;
		}

		if (y && Math.sign(event.detail.position.y) !== -1 !== negate) {
			return true;
		}

		return false;
	}

	private mapStickToAngle(
		position: { x: number; y: number },
		xParams?: [number, number, number],
		yParams?: [number, number, number],
	): number {
		const [min, max, step] = xParams || yParams || [0, 0, 1];
		const value = xParams ? position.x : position.y;
		return Math.round(((value + 1) * (max - min) / 2 + min) / step) * step;
	}

	private sendAction(action: string, data: Record<string, unknown>): void {
		this.appService.socket.send(JSON.stringify({
			to: 'rc-car-server',
			data: { action, ...data },
		}));
	}

	private updateDegreeOfTurns(change: number): void {
		this.degreeOfTurns += change;
		this.sendAction('change-degree-of-turns', { degreeOfTurns: this.degreeOfTurns });
	}

	private stopCar(): void {
		this.sendAction('stop-car', {});
	}

	private startCamera(): void {
		this.sendAction('run-mediamtx', {});
	}

	private stopCamera(): void {
		this.sendAction('stop-mediamtx', {});
	}

	private setEscToNeutralPosition(): void {
		this.sendAction('set-esc-to-neutral-position', {});
	}

	private breakCar(): void {
		this.sendAction('breaking', {});
	}
}
