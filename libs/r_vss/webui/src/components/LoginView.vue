<template>
  <div class="login-screen">
    <div class="login-card">
      <img src="/logos/revere-lockup-white.svg" alt="Revere" class="logo" />

      <!-- first run: no system password yet -->
      <template v-if="passwordSet === false">
        <p class="sub">Welcome. Create a system password to secure access.</p>
        <input
          type="password"
          v-model="password"
          placeholder="New password"
          autocomplete="new-password"
          :disabled="busy"
        />
        <input
          type="password"
          v-model="confirm"
          placeholder="Confirm password"
          autocomplete="new-password"
          :disabled="busy"
          @keyup.enter="doCreate"
        />
        <button class="primary" :disabled="busy || !canCreate" @click="doCreate">
          {{ busy ? 'Creating…' : 'Create & sign in' }}
        </button>
        <p v-if="!canCreate && confirm" class="hint">Passwords don’t match.</p>
      </template>

      <!-- normal: sign in -->
      <template v-else>
        <p class="sub">Sign in to continue.</p>
        <input
          type="password"
          v-model="password"
          placeholder="Password"
          autocomplete="current-password"
          :disabled="busy"
          @keyup.enter="doLogin"
        />
        <button class="primary" :disabled="busy || !password" @click="doLogin">
          {{ busy ? 'Signing in…' : 'Sign in' }}
        </button>
      </template>

      <p v-if="error" class="err">{{ error }}</p>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { login, setSystemPassword } from '../api.js'

const props = defineProps({
  // null = unknown/checking, true = password exists, false = first run
  passwordSet: { type: Boolean, default: true },
})
const emit = defineEmits(['authenticated'])

const password = ref('')
const confirm = ref('')
const busy = ref(false)
const error = ref('')

const canCreate = computed(() => password.value.length > 0 && password.value === confirm.value)

async function doLogin() {
  if (!password.value || busy.value) return
  busy.value = true
  error.value = ''
  try {
    await login(password.value)
    emit('authenticated')
  } catch (e) {
    error.value = e.message
    password.value = ''
  } finally {
    busy.value = false
  }
}

async function doCreate() {
  if (!canCreate.value || busy.value) return
  busy.value = true
  error.value = ''
  try {
    await setSystemPassword(password.value)
    // First-run convenience: sign straight in with the password just set.
    await login(password.value)
    emit('authenticated')
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = false
  }
}
</script>

<style scoped>
.login-screen {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #111;
}
.login-card {
  width: 320px;
  max-width: calc(100vw - 2rem);
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  padding: 1.75rem;
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 10px;
}
.login-card .logo {
  height: 34px;
  align-self: center;
  margin-bottom: 0.5rem;
}
.sub { color: #aaa; font-size: 0.9rem; text-align: center; }
.login-card input {
  background: #111;
  border: 1px solid #333;
  border-radius: 5px;
  color: #eee;
  padding: 0.55rem 0.65rem;
  font-size: 0.95rem;
}
.login-card input:focus { outline: none; border-color: #2d6cdf; }
.primary {
  background: #2d6cdf;
  border: none;
  color: #fff;
  padding: 0.55rem 1rem;
  border-radius: 5px;
  font-size: 0.95rem;
  cursor: pointer;
  margin-top: 0.25rem;
}
.primary:disabled { background: #333; color: #777; cursor: default; }
.hint { color: #c8a13a; font-size: 0.8rem; }
.err { color: #e05555; font-size: 0.85rem; }
</style>
