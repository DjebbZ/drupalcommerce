<?php
// $Id$

/**
 * The page and form callbacks for use in the checkout form.
 */


/**
 * Build the checkout form based on the current step in checkout.
 */
function commerce_checkout_form(&$form_state = array()) {
  drupal_add_css(drupal_get_path('module', 'commerce_checkout') .'/theme/commerce_checkout.css');

  global $user;

  $form = array('#validate' => array());

  $form['uid'] = array(
    '#type' => 'hidden',
    '#value' => $user->uid,
  );

  $account = drupal_clone($user);

  $form['account'] = array(
    '#type' => 'value',
    '#value' => $account,
  );

  // Load existing checkout order data if possible.
  if (!empty($form_state['storage']['order'])) {
    $order = $form_state['storage']['order'];
  }
  else {
    // TODO: Check for an order ID and checkout step in the session to recover
    // in the event of a page refresh.
    $order = FALSE;
  }

  $form['order'] = array(
    '#type' => 'value',
    '#value' => $order,
  );

  // Add the current cart data to the form, taking price alterers into account.
  $context = array(
    'revision' => 'altered',
    'location' => 'cart-checkout-item',
  );

  // TODO: Update this to use the shopping cart order.
  $contents = commerce_cart_get_contents();

  foreach ($contents as $key => $item) {
    $price_info = array(
      'price' => $item->price,
      'qty' => $item->qty,
    );
    $context['subject'] = array('cart_item' => $item);
    $context['extras'] = array('node' => node_load($item->nid));

    // Get the altered price per unit, as ordered products have a locked-in
    // price. Price altering rules may change over time, but the amount paid
    // by the customer does not after the fact.
    $contents[$key]->price = commerce_price($price_info, $context) / $item->qty;
  }

  $form['cart_contents'] = array(
    '#type' => 'value',
    '#value' => $contents,
  );

  // Load data for the current page in checkout.
  $number = isset($form_state['values']) ? $form_state['storage']['page_number'] : 1;
  $page = commerce_checkout_page_load($number);

  // Update the page title if specified.
  if (!empty($page['title'])) {
    drupal_set_title(check_plain($page['title']));
  }

  $form['page'] = array(
    '#type' => 'value',
    '#value' => $page,
  );

  // Add the instructions for the checkout page if specified.
  $form['instructions'] = array(
    '#value' => '<div id="checkout-page-'. $number .'-instructions">'. filter_xss($page['instructions']) .'</div>',
  );

  // If we're on one of the defined checkout form pages...
  if (is_numeric($number)) {
    // Add any enabled checkout panes for this page to the form.
    foreach (commerce_checkout_pane_list(array('enabled' => TRUE, 'page' => $number)) as $pane_id => $pane) {
      if ($callback = commerce_checkout_pane_callback('checkout', $pane_id, 'form')) {
        // Create a fieldset for the pane.
        $form[$pane_id] = array(
          '#type' => 'fieldset',
          '#title' => !empty($pane['title']) ? check_plain($pane['title']) : $pane['name'],
          '#collapsible' => $pane['collapsible'],
          '#collapsed' => $pane['collapsed'],
          '#attributes' => array('class' => $pane_id),
          '#tree' => TRUE,
        );

        // Add the form data defined in the pane's form callback.
        $form[$pane_id] += $callback($form_state, $order);

        // Add the validate handler for the pane to the form array.
        if ($callback = commerce_checkout_pane_callback('checkout', $pane_id, 'form_validate')) {
          $form['#validate'][] = $callback;
        }
      }
    }
  }
  elseif ($number == 'review') {
    // Otherwise add any enabled checkout panes that are visible on review.
    $form['review'] = array(
      '#theme' => 'commerce_checkout_review',
      '#data' => array(),
    );

    // Load all the checkout panes enabled for the review page.
    $panes = commerce_checkout_pane_list(array('enabled' => TRUE, 'review' => TRUE));

    // Find the number of pages.
    $pages = commerce_checkout_page_load();

    // Loop through all the pages...
    foreach (array_keys($pages) as $review_page) {
      // Then loop through all the panes...
      foreach ($panes as $pane_id => $pane) {
        // And if the pane is on the current page and has a review callback...
        if ($pane['page'] == $review_page && $callback = commerce_checkout_pane_callback('checkout', $pane_id, 'review')) {
          // Add a row for it in the review data.
          $form['review']['#data'][$pane_id] = array(
            'title' => !empty($pane['title']) ? check_plain($pane['title']) : $pane['name'],
          );

          // Get the review data for this pane.
          $form['review']['#data'][$pane_id]['data'] = $callback($form_state, $order);
        }
      }
    }
  }

  $form['buttons'] = array(
    '#type' => 'fieldset',
    '#attributes' => array('class' => 'checkout-buttons'),
  );

  // Add a review class to the buttons fieldset on the review page.
  if ($number == 'review') {
    $form['buttons']['#attributes']['class'] .= ' review';
  }

  // Add the cancel or back button where appropriate.
  if (!$page['prev_page'] && !empty($page['cancel'])) {
    $form['buttons']['cancel'] = array(
      '#type' => 'submit',
      '#value' => $page['cancel'],
      '#validate' => array(),
      '#submit' => array('commerce_checkout_form_cancel_submit'),
    );
  }
  elseif ($page['prev_page'] && !empty($page['back'])) {
    $form['buttons']['back'] = array(
      '#type' => 'submit',
      '#value' => $page['back'],
    );
  }

  $form['buttons']['continue'] = array(
    '#type' => 'submit',
    '#value' => $page['continue'],
  );

  return $form;
}

/**
 * Submit handler for the continue and back buttons of the checkout form.
 */
function commerce_checkout_form_submit($form, &$form_state) {
  global $user;

  // Initialize the work in progress order object if it does not already exist.
  // TODO: Update for the new shopping cart order system.
  if (!$form_state['storage']['order']) {
    $form_state['storage']['order'] = commerce_order_new($form_state['values']['uid']);
    $form_state['storage']['order']->products = $form_state['values']['cart_contents'];
    $form_state['storage']['order']->order_total = commerce_order_get_total($form_state['storage']['order'], TRUE);
  }

  // Loop through the enabled checkout panes.
  foreach (commerce_checkout_pane_list(array('enabled' => TRUE, 'page' => $form_state['values']['page']['number'])) as $pane_id => $pane) {
    // If the pane has defined a checkout form submit handler...
    if ($callback = commerce_checkout_pane_callback('checkout', $pane_id, 'form_submit')) {
      // Give it a chance to process the submitted data.
      $callback($form, $form_state);
    }
  }

  // Save the updated order object.
  commerce_order_save($form_state['storage']['order']);

  // Determine which button was used to submit the form.
  switch($form_state['values']['op']) {
    // Customer clicked the back button.
    case $form_state['values']['back']:
      // If there is a previous page...
      if ($form_state['values']['page']['prev_page']) {
        // Move the form back to that page.
        $form_state['storage']['page_number'] = $form_state['values']['page']['prev_page'];
      }
      break;

    // Customer clicked the continue button.
    case $form_state['values']['continue']:
      // If there is another checkout page...
      if ($form_state['values']['page']['next_page']) {
        // Move the form forward to that page.
        $form_state['storage']['page_number'] = $form_state['values']['page']['next_page'];
      }
      elseif (variable_get('commerce_checkout_review_status', TRUE) && $form_state['storage']['page_number'] != 'review') {
        // Otherwise, head to the review page if it's enabled.
        $form_state['storage']['page_number'] = 'review';
      }
      else {
        // Or go ahead and complete the sale.
        $order = $form_state['storage']['order'];

        // Determine what to do for anonymous users completing checkout.
        if ($order->uid == 0) {
          $create = variable_get('commerce_checkout_anonymous_account', 2);
          $notify = variable_get('commerce_checkout_anonymous_notify', 3);

          if ($create > 0) {
            $account = commerce_checkout_create_account($name, $mail, '', $create == 1 ? 0 : 1, $notify >= 2);

            $order->uid = $account->uid;
            commerce_order_save($order);
          }
          else {
            $account = FALSE;
          }
        }

        // Move an order's status from "In Checkout" to "Pending"
        /**
         * TODO: Update to use the new order status API.
         *
        $status = db_result(db_query("SELECT order_status FROM {uc_orders} WHERE order_id = %d", $order->order_id));
        if (uc_order_status_data($status, 'state') == 'in_checkout') {
          uc_order_update_status($order->order_id, uc_order_state_default('post_checkout'));
        }
         */

        // TODO: Update to use Rules; the third argument should pass the proper account.
        // ca_pull_trigger('uc_checkout_complete', $order, $order->uid == 0 && $account ? $account : $user);

        // Empty the cart and the form storage and proceed to completion.
        $form_state['redirect'] = 'checkout/complete/'. $form_state['storage']['order']->order_id;

        unset($form_state['storage']);
        // commerce_cart_empty(uc_cart_get_id());
      }
      break;
  }
}

/**
 * Special submit handler for the cancel button to avoid processing orders.
 */
function commerce_checkout_form_cancel_submit($form, &$form_state) {
  if ($form_state['values']['order']) {
    // TODO: Implement order comments to do this.
    // commerce_order_comment_save($form_state['values']['order']->order_id, 0, t('Customer cancelled this order from the checkout form.'));
  }

  drupal_set_message(t('Your order has been canceled.'));
  unset($form_state['storage']);

  $form_state['redirect'] = 'cart';
}

/**
 * Theme the optional checkout review page data.
 */
function theme_commerce_checkout_review($element) {
  // return print_r($element['#data'], TRUE);
  drupal_add_css(drupal_get_path('module', 'commerce_checkout') .'/theme/commerce_checkout.css');

  // Turn the review data array into table rows.
  $rows = array();

  foreach ($element['#data'] as $pane_id => $data) {
    // First add a row for the title.
    $rows[] = array(
      'data' => array(
        array('data' => $data['title'], 'colspan' => 2),
      ),
      'class' => 'pane-title odd',
    );

    // Next, add the data for this particular section.
    if (is_array($data['data'])) {
      // If it's an array, treat each key / value pair as a 2 column row.
      foreach ($data['data'] as $key => $value) {
        $rows[] = array(
          'data' => array(
            array('data' => $key .':', 'class' => 'pane-data-key'),
            array('data' => $value, 'class' => 'pane-data-value'),
          ),
          'class' => 'pane-data even',
        );
      }
    }
    else {
      // Otherwise treat it as a block of text in its own row.
      $rows[] = array(
        'data' => array(
          array('data' => $data['data'], 'colspan' => 2, 'class' => 'pane-data-full'),
        ),
        'class' => 'pane-data even',
      );
    }
  }

  return theme('table', array(), $rows, array('class' => 'checkout-review'));
}

/**
 * Displays the checkout completion page for an order.
 */
function commerce_checkout_complete($order) {
  return t('Your order is complete! This is placeholder text for order completion message.');
}