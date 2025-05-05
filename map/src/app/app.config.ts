import { ApplicationConfig } from '@angular/core';
import { provideRouter } from '@angular/router';

import { routes } from './app.routes';
import { provideAnimationsAsync } from '@angular/platform-browser/animations/async';
import { provideMapboxGL } from 'ngx-mapbox-gl';

export const appConfig: ApplicationConfig = {
  providers: [
    provideRouter(routes),
    provideAnimationsAsync(),
    provideMapboxGL({
      accessToken: import.meta.env.NG_APP_MAPBOX_ACCESS_TOKEN
    }),
  ]
};
