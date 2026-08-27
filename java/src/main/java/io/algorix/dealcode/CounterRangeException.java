package io.algorix.dealcode;

/**
 * Thrown by {@link Dealcode#encode(long)} when the counter is negative or
 * exceeds {@link Dealcode#maxCounter()} (SPEC §5, §8 — the spec's
 * {@code RangeError}).
 */
public class CounterRangeException extends DealcodeException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates a new exception with the given detail message.
     *
     * @param message the detail message
     */
    public CounterRangeException(String message) {
        super(message);
    }
}
