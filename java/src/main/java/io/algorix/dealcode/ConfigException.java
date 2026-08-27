package io.algorix.dealcode;

/**
 * Thrown at {@linkplain Dealcode.Builder#build() construction time} when a
 * codec configuration is invalid: empty key material, an unusable alphabet,
 * out-of-bounds lengths, or an over-long domain (SPEC §2, §8).
 */
public class ConfigException extends DealcodeException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates a new exception with the given detail message.
     *
     * @param message the detail message
     */
    public ConfigException(String message) {
        super(message);
    }
}
