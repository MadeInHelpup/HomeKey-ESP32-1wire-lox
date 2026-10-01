<script lang="ts">
  import { saveLoxoneConfig, type LoxoneConfig, type LoxoneConfigResult } from '$lib/services/loxone';

  let { result }: { result?: LoxoneConfigResult } = $props();

  // svelte-ignore state_referenced_locally
  const initial: LoxoneConfig | null = result?.status === 'ok' ? result.config : null;

  let saved = $state<LoxoneConfig | null>(initial && $state.snapshot(initial));
  let form = $state<LoxoneConfig | null>(initial && $state.snapshot(initial));
  let saving = $state(false);

  const save = async (e: Event) => {
    e.preventDefault();
    if (!form) return;
    saving = true;
    const stored = await saveLoxoneConfig($state.snapshot(form));
    if (stored) {
      saved = stored;
      form = { ...stored };
    }
    saving = false;
  };

  const resetForm = () => {
    if (saved) form = { ...saved };
  };
</script>

<div class="w-full py-6">
  <div class="mb-6">
    <h1 class="text-2xl font-bold text-base-content flex items-center gap-2">
      Loxone 1-Wire
      <div class="tooltip tooltip-bottom tooltip-info" data-tip="Device will reboot to apply changes!">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="size-5 text-info">
          <path stroke-linecap="round" stroke-linejoin="round" d="m3.75 13.5 10.5-11.25L12 10.5h8.25L9.75 21.75 12 13.5H3.75Z" />
        </svg>
      </div>
    </h1>
    <p class="text-sm text-base-content/60">Present a virtual DS1990A iButton on a 1-Wire bus after each HomeKey tap, e.g. for a Loxone 1-Wire Extension.</p>
  </div>

  {#if result?.status === 'unavailable'}
    <div class="alert alert-warning max-w-2xl">
      <span>The Loxone 1-Wire bridge is not part of this firmware. Build with <code>CONFIG_LOXONE_ONEWIRE=y</code> to enable it.</span>
    </div>
  {:else if !form}
    <div class="text-center text-error">
      <p>Error: {result?.status === 'error' ? result.error : 'Failed to load the configuration'}</p>
    </div>
  {:else}
    <form onsubmit={save} class="max-w-2xl space-y-6">
      <div class="card bg-base-200 rounded-xl p-4">
        <label class="flex items-center justify-between cursor-pointer">
          <div>
            <span class="font-medium">Enable 1-Wire bridge</span>
            <p class="text-sm text-base-content/60">Emulate a DS1990A iButton on the 1-Wire bus after each HomeKey tap.</p>
          </div>
          <input type="checkbox" class="toggle toggle-success ml-4" bind:checked={form.enabled} />
        </label>
      </div>

      <div class="card bg-base-200 rounded-xl p-4 space-y-4" class:opacity-50={!form.enabled}>
        <div class="form-control">
          <label class="label" for="gpioPin">
            <span class="label-text font-medium">1-Wire GPIO pin</span>
            <span class="label-text-alt text-base-content/60">Must be output capable (not GPIO 34-39 on the ESP32)</span>
          </label>
          <input id="gpioPin" type="number" class="input input-bordered w-32" min="0" max="48" disabled={!form.enabled} bind:value={form.gpioPin} />
          <p class="label"><span class="label-text-alt">Connect an external 4.7 kOhm pull-up resistor to 3.3 V on this pin.</span></p>
        </div>

        <div class="form-control">
          <label class="label" for="activeDuration">
            <span class="label-text font-medium">Active window (ms)</span>
            <span class="label-text-alt text-base-content/60">How long the iButton stays on the bus after a tap</span>
          </label>
          <input id="activeDuration" type="number" class="input input-bordered w-40" min="500" max="4500" step="100" disabled={!form.enabled} bind:value={form.activeDurationMs} />
          <p class="label"><span class="label-text-alt">Loxone polls about once per second, 3000 ms gives two to three read cycles. Maximum 4500 ms.</span></p>
        </div>

        <div class="form-control">
          <label class="label" for="romSource">
            <span class="label-text font-medium">ROM source</span>
            <span class="label-text-alt text-base-content/60">Which ID the iButton ROM is derived from</span>
          </label>
          <select id="romSource" class="select select-bordered w-72" disabled={!form.enabled} bind:value={form.romSource}>
            <option value={0}>Apple ID (issuerId), all devices of a person</option>
            <option value={1}>Device (endpointId), this device only</option>
          </select>
          <p class="label">
            <span class="label-text-alt">
              {#if form.romSource === 0}
                iPhone, Apple Watch and iPad of the same Apple ID share one ROM, so one Loxone rule covers all of them.
              {:else}
                Every physical device gets its own ROM, so access can be granted or revoked per device.
              {/if}
            </span>
          </p>
        </div>
      </div>

      <div class="alert alert-info">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
        </svg>
        <div class="text-sm">
          <p class="font-medium mb-1">How the iButton code is generated</p>
          <p>
            After a tap the ESP32 derives a deterministic 8-byte ROM: <code>0x01</code>, six bytes of the chosen ID and a CRC8.
            It stays the same across reboots and reflashes. To register a person, tap their device and run the
            1-Wire search in Loxone Config; the ROM is printed in the log as well.
          </p>
          <p class="mt-1">
            An iButton ROM is not a secret: anyone who can reach the 1-Wire bus and knows the ROM can present it.
            Keep the bus wiring physically protected.
          </p>
        </div>
      </div>

      <div class="flex gap-3">
        <button type="submit" class="btn btn-primary" disabled={saving}>Save &amp; reboot</button>
        <button type="button" class="btn btn-ghost" onclick={resetForm} disabled={saving}>Reset</button>
      </div>
    </form>
  {/if}
</div>
