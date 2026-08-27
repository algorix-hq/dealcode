package io.algorix.dealcode;

/**
 * Base class for all dealcode errors.
 *
 * <p>Extends {@link IllegalArgumentException} because every dealcode failure
 * is, at heart, invalid input: a bad configuration ({@link ConfigException}),
 * a counter outside the encodable range ({@link CounterRangeException}), or a
 * code string this codec never issued ({@link InvalidCodeException}). Catch
 * this type to handle all three uniformly.</p>
 */
public class DealcodeException extends IllegalArgumentException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates a new exception with the given detail message.
     *
     * @param message the detail message
     */
    public DealcodeException(String message) {
        super(message);
    }

    /**
     * Creates a new exception with the given detail message and cause.
     *
     * @param message the detail message
     * @param cause the underlying cause
     */
    public DealcodeException(String message, Throwable cause) {
        super(message, cause);
    }
}
