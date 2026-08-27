package io.algorix.dealcode;

/**
 * Thrown by {@link Dealcode#decode(String)} when a code string fails the
 * length, character-set, or stage/counter-range checks — i.e. this codec can
 * never have issued it (SPEC §7, §8).
 *
 * <p>Note that decode rejecting a string does not mean the string "looks
 * wrong": a well-formed but unissued code decrypts to an out-of-range value
 * and is rejected here. Decode success only proves the code is
 * <em>consistent</em> with the key; the application still decides whether the
 * returned counter actually exists.</p>
 */
public class InvalidCodeException extends DealcodeException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates a new exception with the given detail message.
     *
     * @param message the detail message
     */
    public InvalidCodeException(String message) {
        super(message);
    }
}
