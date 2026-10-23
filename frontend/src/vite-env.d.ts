/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** Optional absolute API URL; leave unset to use the same-origin /api proxy. */
  readonly VITE_API_BASE_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
