import type { Hooks } from 'sv-router';
import { fetchLoxoneConfig, type LoxoneConfigResult } from '$lib/services/loxone';
import { setLoadingState } from '$lib/stores/system.svelte';

declare module 'sv-router' {
  interface RouteMeta {
    loxoneData?: LoxoneConfigResult;
  }
}

export default {
  async beforeLoad({ meta }) {
    try {
      setLoadingState(true);
      meta.loxoneData = await fetchLoxoneConfig();
    } finally {
      setLoadingState(false);
    }
  },
} satisfies Hooks;
