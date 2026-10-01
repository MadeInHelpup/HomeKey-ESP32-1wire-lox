<script lang="ts">
	import AppLoxone from '$lib/components/AppLoxone.svelte';
	import { route } from 'sv-router/generated';
</script>

<AppLoxone result={route.meta.loxoneData} />
