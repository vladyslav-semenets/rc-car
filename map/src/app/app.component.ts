import { Component, inject, OnInit, signal, WritableSignal } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { AppService } from "./app.service";
import { MapComponent } from 'ngx-mapbox-gl';
import { LngLatLike, Map, Marker } from 'mapbox-gl';
import { animate, style, transition, trigger } from '@angular/animations';

@Component({
    selector: 'app-root',
    imports: [
        RouterOutlet,
	    MapComponent,
    ],
    templateUrl: './app.component.html',
    styleUrl: './app.component.scss',
	animations: [
		trigger('speedChanged', [
			transition(':increment', [
				style({ opacity: 0, transform: 'translateY(5px)' }),
				animate('200ms ease-out', style({ opacity: 1, transform: 'translateY(0)' })),
			]),
			transition(':decrement', [
				style({ opacity: 0, transform: 'translateY(-5px)' }),
				animate('200ms ease-out', style({ opacity: 1, transform: 'translateY(0)' })),
			]),
		]),
	],
})
export class AppComponent implements OnInit {
	public center: WritableSignal<[number, number]> = signal([18.48, 54.22]);
	public speed: WritableSignal<number> = signal(0);
	public zoom: WritableSignal<[number]> = signal([7]);
	public mapStyle: string = 'mapbox://styles/mapbox/satellite-streets-v12';
	private _map!: Map;
	public _carMarker!: Marker;
	private readonly _appService: AppService = inject(AppService);

	constructor() {
		this._appService.initWebSocket();
	}

	public ngOnInit(): void {
		this._appService.socket.addEventListener(
			'message',
			(event) => {
				const payload = JSON.parse(event.data);

				if (payload.latitude && payload.longitude && payload.speed) {
					this._setCarMarker([payload.longitude, payload.latitude])
					this.speed.update(() => parseInt(payload.speed, 10));
				}
			});
	}

	public onMapCreate(map: Map) {
		this._map = map;
	}

	private _setCarMarker(lnglat: LngLatLike): void {
		if (this._carMarker) {
			this._carMarker.setLngLat(lnglat);
		} else {
			this._carMarker = new Marker({ color: 'black' })
				.setLngLat(lnglat)
				.addTo(this._map);
		}

		this._map.flyTo({
			center: lnglat,
			zoom: 14,
			speed: 1.2,
			curve: 1.42,
		});
	}
}
