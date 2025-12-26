<script lang="ts">
  import { onMount } from 'svelte';
  import System from './pages/System.svelte';
  import Config from './pages/Config.svelte';
  import Decoder from './pages/Decoder.svelte';

  let currentPage = $state('/');

  onMount(() => {
    currentPage = window.location.pathname;

    window.addEventListener('popstate', () => {
      currentPage = window.location.pathname;
    });
  });

  function navigate(path: string) {
    history.pushState({}, '', path);
    currentPage = path;
  }
</script>

<div class="app">
  <nav>
    <a href="/" onclick={(e) => { e.preventDefault(); navigate('/'); }}>Home</a>
    <a href="/config" onclick={(e) => { e.preventDefault(); navigate('/config'); }}>Config</a>
    <a href="/system" onclick={(e) => { e.preventDefault(); navigate('/system'); }}>System</a>
    <a href="/keyer" onclick={(e) => { e.preventDefault(); navigate('/keyer'); }}>Keyer</a>
    <a href="/decoder" onclick={(e) => { e.preventDefault(); navigate('/decoder'); }}>Decoder</a>
    <a href="/timeline" onclick={(e) => { e.preventDefault(); navigate('/timeline'); }}>Timeline</a>
  </nav>

  <main>
    {#if currentPage === '/system'}
      <System />
    {:else if currentPage === '/config'}
      <Config />
    {:else if currentPage === '/keyer'}
      <h1>Keyer</h1>
      <p>Keyer page - coming soon</p>
    {:else if currentPage === '/decoder'}
      <Decoder />
    {:else if currentPage === '/timeline'}
      <h1>Timeline</h1>
      <p>Timeline page - coming soon</p>
    {:else}
      <h1>CW Keyer</h1>
      <p>Welcome to the CW Keyer WebUI</p>
    {/if}
  </main>
</div>

<style>
  .app {
    font-family: system-ui, sans-serif;
    max-width: 1200px;
    margin: 0 auto;
    padding: 1rem;
  }

  nav {
    display: flex;
    gap: 1rem;
    padding: 1rem;
    background: #f0f0f0;
    border-radius: 8px;
    margin-bottom: 1rem;
  }

  nav a {
    color: #333;
    text-decoration: none;
    padding: 0.5rem 1rem;
    border-radius: 4px;
  }

  nav a:hover {
    background: #ddd;
  }

  main {
    padding: 1rem;
  }
</style>
