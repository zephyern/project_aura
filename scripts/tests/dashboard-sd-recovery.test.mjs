import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const root = new URL('../../', import.meta.url);
const source = readFileSync(new URL('src/web/WebTemplatesDashboardAp.cpp', root), 'utf8');
const script = source.match(/<script>\r?\n([\s\S]*?)<\/script>/)?.[1];
assert.ok(script);
new vm.Script(script);
const sdCode = script.slice(script.indexOf('function setSdNote('), script.indexOf("let activeTab = 'sensors';"));
const healthy = () => ({
  sd: {mounted:true, ready:true, can_reconnect:true, state:'mounted', runtime_healthy:true,
       recovery_attempts:2, recovery_successes:1, has_successful_write:true, last_write_age_ms:9000},
  daily: {current_day:20260928, current_sample_count:120, last_write_ok:true},
  files: {daily_csv:{exists:true, size_bytes:123}, current_day_state:{exists:true, size_bytes:200}},
});
function harness(payload=healthy()) {
  const elements = new Map(), calls=[];
  const get = id => {
    if (!elements.has(id)) elements.set(id, {textContent:'', disabled:false, className:'', attrs:{},
      classList:{toggle(){}}, setAttribute(k,v){this.attrs[k]=v;}, removeAttribute(k){delete this.attrs[k];}});
    return elements.get(id);
  };
  const context = vm.createContext({
    document:{getElementById:get}, window:{confirm:()=>true},
    sdHistoryRefreshBusy:false, sdHistoryStatusCache:null, sdReconnectBusy:false, sdServiceBusy:false,
    otaUploadInFlight:false, otaAwaitingDeviceOutcome:false, otaRestartPending:false,
    isNum:Number.isFinite, formatBytes:String, formatDayKey:String,
    formatFileStatus:x=>x?.error || (x?.exists ? 'Present':'--'),
    setInfoValue:(id,text,cls)=>Object.assign(get(id),{textContent:text,className:cls}),
    getJson:async url=>{calls.push(['GET',url]); return payload;},
    postJson:async url=>{calls.push(['POST',url]); return {success:true,queued:true};},
  });
  vm.runInContext(sdCode, context);
  return {context,get,calls,payload};
}

test('waiting recovery blocks SD writes/downloads but preserves RAM export', () => {
  const h=harness(); h.payload.sd.ready=false; h.payload.sd.recovery_pending=true; h.payload.sd.recovery_retry_ms=5000;
  h.context.renderSdHistoryStatus(h.payload);
  assert.equal(h.get('sd-status').textContent, 'Waiting to reconnect');
  assert.match(h.get('sd-note').textContent, /in 5 s/);
  assert.equal(h.get('sdExportCurrentBtn').attrs['aria-disabled'],'false');
  assert.equal(h.get('sdExportCsvBtn').attrs['aria-disabled'],'true');
  assert.equal(h.get('sdClearHistoryBtn').disabled,true);
  assert.equal(h.get('sdReconnectBtn').disabled,false);
});
test('a mount alone is shown as awaiting a history save', () => {
  const h=harness(); h.payload.sd.write_verification_pending=true;
  h.context.renderSdHistoryStatus(h.payload);
  assert.equal(h.get('sd-status').textContent,'Mounted - awaiting save');
  assert.match(h.get('sd-note').textContent,/Waiting for a successful history save/);
  assert.equal(h.get('sd-recovery').textContent,'1 reconnected / 2 attempts');
  assert.equal(h.get('sd-last-saved').textContent,'9 s ago');
});
test('changed media cannot expose a destructive history action', () => {
  const h=harness(); Object.assign(h.payload.sd,{ready:false,recovery_blocked:true,last_error:'Export current day, then restart'});
  h.context.renderSdHistoryStatus(h.payload);
  assert.equal(h.get('sd-status').textContent,'History paused');
  assert.equal(h.get('sdClearHistoryBtn').disabled,true);
  assert.equal(h.get('sdExportCsvBtn').attrs['aria-disabled'],'true');
  assert.equal(h.get('sdExportCurrentBtn').attrs['aria-disabled'],'false');
  assert.match(h.get('sd-note').textContent,/Export current day/);
});
test('manual reconnect posts once, stays busy while waiting, then displays scheduled recovery', async () => {
  const h=harness(); let finish; let count=0;
  h.context.postJson=async url=>{assert.equal(url,'/api/history/sd/reconnect'); ++count; return new Promise(resolve=>{finish=resolve;});};
  Object.assign(h.payload.sd,{ready:false,recovery_pending:true});
  const pending=h.context.reconnectSd();
  assert.equal(h.get('sdReconnectBtn').disabled,true);
  assert.equal(h.get('sdReconnectBtn').attrs['aria-busy'],'true');
  await h.context.reconnectSd(); assert.equal(count,1);
  finish({success:true}); await pending;
  assert.equal(h.get('sdReconnectBtn').disabled,false);
  assert.equal(h.get('sd-status').textContent,'Waiting to reconnect');
});
test('failed reconnect retains its error and restores controls from known card state', async () => {
  const h=harness(); h.context.renderSdHistoryStatus(h.payload);
  h.context.postJson=async()=>{throw new Error('OTA is busy');};
  await h.context.reconnectSd();
  assert.equal(h.get('sd-note').textContent,'OTA is busy');
  assert.equal(h.get('sdReconnectBtn').attrs['aria-busy'],'false');
  assert.equal(h.get('sdReconnectBtn').disabled,false);
});
test('OTA prevents a reconnect request and status refresh remains GET only', async () => {
  const h=harness(); h.context.otaUploadInFlight=true;
  await h.context.reconnectSd(); assert.equal(h.calls.length,0);
  h.context.otaUploadInFlight=false;
  await h.context.refreshSdHistory();
  assert.deepEqual(h.calls,[['GET','/api/history/daily/status']]);
});
test('reconnect is POST-only and never enables automatic formatting', () => {
  const routes=readFileSync(new URL('src/web/WebRouteRegistry.cpp',root),'utf8');
  assert.match(routes,/onPost\("\/api\/history\/sd\/reconnect", daily_history_reconnect_handle\)/);
  assert.doesNotMatch(routes,/onGet\("\/api\/history\/sd\/reconnect"/);
  const manager=readFileSync(new URL('src/modules/SdCardManager.cpp',root),'utf8');
  assert.match(manager,/format_if_mount_failed = false;/);
  assert.doesNotMatch(manager,/format_if_mount_failed = true|esp_restart\(/);
});

test('manual reconnect remains available during the five-minute retry delay', async () => {
  const h=harness();
  Object.assign(h.payload.sd,{mounted:false,ready:false,state:'not_detected',
    recovery_pending:true,recovery_retry_ms:300000,can_reconnect:true});
  h.context.renderSdHistoryStatus(h.payload);
  assert.equal(h.get('sdReconnectBtn').disabled,false);
  assert.equal(h.get('sdReconnectBtn').attrs['aria-busy'],'false');
  await h.context.reconnectSd();
  assert.deepEqual(h.calls,[['POST','/api/history/sd/reconnect'],['GET','/api/history/daily/status']]);
});

test('active recovery and paused recovery keep manual reconnect disabled', () => {
  for (const state of [{recovering:true},{recovery_pending:true,recovery_paused:true}]) {
    const h=harness(); Object.assign(h.payload.sd,state);
    h.context.renderSdHistoryStatus(h.payload);
    assert.equal(h.get('sdReconnectBtn').disabled,true);
  }
});

test('long-term history on: clear confirms name the long-term files and the first day', () => {
  const h=harness();
  const payload={...healthy(), long_history:{first_day:'2026-11-07'}};
  payload.daily={...payload.daily, pending_day_count:0, pending_retry_ms:0, dropped_pending_days:0,
    current_day_units_c:true, preferred_units_c:true};
  h.context.renderSdHistoryStatus(payload);
  assert.equal(h.context.sdClearHistoryConfirmText(payload),
    'Delete ALL long-term history on the SD card? Every 15-minute, hourly, daily and weekly file since 2026-11-07, and the old daily file, will be deleted. This cannot be undone.');
  assert.equal(h.context.sdClearCurrentConfirmText(payload), "Delete today's history only? Earlier days are kept.");
  assert.equal(h.get('sdClearHistoryBtn').disabled,false);
  assert.equal(h.get('sdExportCsvBtn').attrs['aria-disabled'],'false');
});

test('long-term history off: clear confirms are unchanged', () => {
  const h=harness();
  assert.equal(h.context.sdClearHistoryConfirmText(healthy()), 'Delete daily CSV and current day history?');
  assert.equal(h.context.sdClearCurrentConfirmText(healthy()), 'Delete current day history snapshot?');
  assert.equal(h.context.sdClearHistoryConfirmText(null), 'Delete daily CSV and current day history?');
});

test('clear future-dated history shows only with the long-term log running', () => {
  const h=harness();
  h.context.renderSdHistoryStatus(healthy());
  assert.equal(h.get('sdClearFutureBtn').hidden,true);
  const payload={...healthy(), long_history:{first_day:'2026-11-07'}};
  h.context.renderSdHistoryStatus(payload);
  assert.equal(h.get('sdClearFutureBtn').hidden,false);
  assert.equal(h.get('sdClearFutureBtn').disabled,false);
});

test('history settings say when a restart is needed', () => {
  const h=harness();
  assert.equal(h.context.historySettingsNote({log_enabled:true, restart_required:true}),
    'Restart to start the long-term history log.');
  assert.equal(h.context.historySettingsNote({log_enabled:false, restart_required:true}),
    'Restart to stop the long-term history log.');
  assert.equal(h.context.historySettingsNote({log_enabled:true, restart_required:false}), '');
});

test('chart settings save sends every field and shows what the device returned', async () => {
  const h=harness();
  const payload=h.context.chartSettingsPayload(id => id === 'chartsBandEnabled');
  assert.deepEqual(JSON.parse(JSON.stringify(payload)), {band_enabled:true, month_enabled:false, year_enabled:false, lifetime_enabled:false});
  h.get('chartsBandEnabled').checked=true;
  h.context.renderChartSettings({success:true, band_enabled:false, month_enabled:true, year_enabled:true,
    lifetime_enabled:true});
  assert.equal(h.get('chartsBandEnabled').checked,false);
  assert.equal(h.get('chartsMonthEnabled').checked,true);
  assert.equal(h.get('chartsYearEnabled').checked,true);
  assert.equal(h.get('chartsLifetimeEnabled').checked,true);
});

test('chart settings save posts to the charts endpoint and reports a refusal', async () => {
  const h=harness();
  h.get('chartsBandEnabled').checked=true;
  h.context.postJson=async (url, body) => { h.calls.push(['POST',url,body]); throw new Error('Unknown field: x'); };
  await h.context.saveChartSettings();
  assert.equal(h.calls.at(-1)[1],'/api/charts/settings');
  assert.equal(h.calls.at(-1)[2].band_enabled,true);
  assert.equal(h.get('chartsSettingsNote').textContent,'Unknown field: x');
});
