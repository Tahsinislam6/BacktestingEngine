// The states of a run, as the server reports them.

// Still waiting or running: keep asking the server about it.
export const isActive = (status) => status === 'queued' || status === 'running';

// Over, one way or another.
export const isFinished = (status) => status === 'done' || status === 'failed' || status === 'cancelled';
