import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'com.rccar.app',
  appName: 'rc-car',
  webDir: 'dist/rc-car/browser',
  server: {
    androidScheme: 'http',
    cleartext: true,
  },
};

export default config;
