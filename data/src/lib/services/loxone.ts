import { notifications } from '../stores/notifications.svelte.js';
import { rebootDevice } from './api';

/**
 * Settings of the optional Loxone 1-Wire bridge (components/loxone_onewire).
 * Served by /loxone_config, which only exists in firmware built with
 * CONFIG_LOXONE_ONEWIRE.
 */
export interface LoxoneConfig {
  /** Present a virtual iButton on the 1-Wire bus after a HomeKey tap */
  enabled: boolean;
  /** GPIO pin of the 1-Wire bus (output capable, external pull-up required) */
  gpioPin: number;
  /** How long the virtual iButton stays on the bus after a tap, in ms */
  activeDurationMs: number;
  /** ROM source: 0 = issuerId (per Apple ID), 1 = endpointId (per device) */
  romSource: number;
}

export type LoxoneConfigResult =
  | { status: 'ok'; config: LoxoneConfig }
  | { status: 'unavailable' }
  | { status: 'error'; error: string };

export async function fetchLoxoneConfig(): Promise<LoxoneConfigResult> {
  try {
    const response = await fetch('/loxone_config');
    // Firmware without the bridge answers unknown URLs with the web UI itself.
    if (!response.headers.get('content-type')?.includes('application/json')) {
      return { status: 'unavailable' };
    }
    const body = await response.json();
    if (!body.success) return { status: 'error', error: body.error ?? 'Unknown error' };
    return { status: 'ok', config: body.data as LoxoneConfig };
  } catch (error) {
    return { status: 'error', error: error instanceof Error ? error.message : 'Unknown error' };
  }
}

/** Saves the settings and reboots the device, the bridge is set up at boot. */
export async function saveLoxoneConfig(config: LoxoneConfig): Promise<LoxoneConfig | null> {
  try {
    const response = await fetch('/loxone_config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config),
    });
    const body = await response.json();
    if (!response.ok || !body.success) {
      notifications.addError(`Failed to save Loxone config: ${body.error ?? response.statusText}`);
      return null;
    }
    notifications.addSuccess(body.message);
    await rebootDevice();
    return body.data as LoxoneConfig;
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Unknown error occurred';
    notifications.addError(`Failed to save Loxone config: ${message}`);
    return null;
  }
}
