declare module "cursor/agent" {
  // These types are provided at runtime by the Cursor extension.
  // We use 'any' to keep the declaration minimal and avoid maintenance.
  export const agent: any;
  export const getFile: any;
  export const exec: any;
  export const watchFile: any;
} 