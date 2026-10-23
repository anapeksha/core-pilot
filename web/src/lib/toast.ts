export function createToastGate() {
  let lastKey = "";

  return (key: string) => {
    if (!key) {
      lastKey = "";
      return false;
    }
    if (key === lastKey) return false;
    lastKey = key;
    return true;
  };
}
