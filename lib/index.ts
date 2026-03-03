import path from 'path';

const bindings = require(path.join(__dirname, '..', 'build', 'Release', 'libjack_node.node'));

export const JackClient: typeof import('./index.d').JackClient = bindings.JackClient;
