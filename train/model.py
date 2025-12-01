import tensorflow as tf

N_FRAMES = 25
N_MFCC = 20


def ds_block(x, filters, stride):
    x = tf.keras.layers.DepthwiseConv2D(3, strides=stride, padding="same", use_bias=False)(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU(6.0)(x)
    x = tf.keras.layers.Conv2D(filters, 1, padding="same", use_bias=False)(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU(6.0)(x)
    return x


def build_model(base_filters=16, num_classes=2):
    inp = tf.keras.Input(shape=(N_FRAMES, N_MFCC, 1), name="mfcc")
    x = tf.keras.layers.Conv2D(base_filters, 3, strides=2, padding="same", use_bias=False)(inp)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU(6.0)(x)

    x = ds_block(x, filters=int(base_filters * 1.5), stride=1)
    x = ds_block(x, filters=int(base_filters * 2.0), stride=2)
    x = ds_block(x, filters=int(base_filters * 3.0), stride=1)

    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    x = tf.keras.layers.Dropout(0.1)(x)
    out = tf.keras.layers.Dense(num_classes, activation="softmax", dtype="float32")(x)
    return tf.keras.Model(inp, out, name="cry_ds_cnn_tiny")
